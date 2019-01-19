/*-----------------------------------------------------------------------------
 * Copyright (c) 2018 by Alexander Reinert
 * Author: Alexander Reinert
 * Uses parts of bcm2835_raw_uart.c. (c) 2015 by eQ-3 Entwicklung GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *---------------------------------------------------------------------------*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/spinlock.h>
#include <linux/circ_buf.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <asm/ioctls.h>
#include <asm/termios.h>
#include <linux/delay.h>

#include "hm.h"

#define  DRIVER_NAME "fake-hmrf"

#define  RX_BUF_SIZE 1024

static ssize_t fake_hmrf_read(struct file *filep, char __user *buf, size_t count, loff_t *offset);
static ssize_t fake_hmrf_write(struct file *filep, const char __user *buf, size_t count, loff_t *offset);
static int fake_hmrf_open(struct inode *inode, struct file *filep);
static int fake_hmrf_close(struct inode *inode, struct file *filep);
static unsigned int fake_hmrf_poll(struct file* filep, poll_table* wait);
static long fake_hmrf_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
static void fake_hmrf_add_to_buffer(char *buf, size_t len);

static struct file_operations fake_hmrf_fops =
{
  .owner = THIS_MODULE,
  .llseek = no_llseek,
  .read = fake_hmrf_read,
  .write = fake_hmrf_write,
  .open = fake_hmrf_open,
  .release = fake_hmrf_close,
  .poll = fake_hmrf_poll,
  .unlocked_ioctl = fake_hmrf_ioctl,
  .compat_ioctl = fake_hmrf_ioctl,
};

static dev_t fake_hmrf_devid;
static struct cdev fake_hmrf_cdev;
static struct class *fake_hmrf_class;
static struct device *fake_hmrf_dev;

static struct circ_buf fake_hmrf_rxbuf;
static wait_queue_head_t fake_hmrf_readq;
static spinlock_t fake_hmrf_writel;

static ssize_t fake_hmrf_read(struct file *filep, char __user *buf, size_t count, loff_t *offset)
{
  int ret = 0;

  while(!CIRC_CNT(fake_hmrf_rxbuf.head, fake_hmrf_rxbuf.tail, RX_BUF_SIZE))
  {
    if(filep->f_flags & O_NONBLOCK )
    {
      ret = -EAGAIN;
      goto exit;
    }

    if(wait_event_interruptible(fake_hmrf_readq, CIRC_CNT(fake_hmrf_rxbuf.head, fake_hmrf_rxbuf.tail, RX_BUF_SIZE)))
    {
      ret = -ERESTARTSYS;
      goto exit;
    }
  }

  count = min((int)count, CIRC_CNT_TO_END(fake_hmrf_rxbuf.head, fake_hmrf_rxbuf.tail, RX_BUF_SIZE) );
  if(copy_to_user(buf, fake_hmrf_rxbuf.buf + fake_hmrf_rxbuf.tail, count))
  {
    ret = -EFAULT;
    goto exit;
  }
  ret = count;

  smp_mb();
  fake_hmrf_rxbuf.tail += count;
  if(fake_hmrf_rxbuf.tail >= RX_BUF_SIZE )
  {
    fake_hmrf_rxbuf.tail -= RX_BUF_SIZE;
  }

exit:
  return ret;
}

static char common_identify_response[]              = { 0x05, 0x01, 0x44, 0x75, 0x61, 0x6C, 0x43, 0x6F, 0x50, 0x72, 0x6F, 0x5F, 0x41, 0x70, 0x70 };
static char common_get_sgtin_response[]             = { 0x05, 0x01, 0x30, 0x14, 0xF7, 0x11, 0xA0, 0x61, 0xA7, 0xD5, 0x69, 0x9D, 0xAB, 0x52 };

static char trx_get_version_response[]              = { 0x04, 0x01, 0x02, 0x08, 0x06, 0x01, 0x00, 0x03, 0x01, 0x14, 0x03 };
static char trx_get_dutycycle_response[]            = { 0x04, 0x01, 0x00 };
static char trx_set_dutycycle_limit_response[]      = { 0x04, 0x01 };
static char trx_get_mcu_type_response[]             = { 0x04, 0x01, 0x03 };

static char llmac_get_default_rf_address_response[] = { 0x01, 0x01, 0x4F, 0x68, 0xF1 };
static char llmac_get_serial_response[]             = { 0x01, 0x01, 0x46, 0x4B, 0x45, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37 };
static char llmac_get_timestamp_response[]          = { 0x01, 0x01, 0x2D, 0xEA };
static char llmac_rfd_init_response[]               = { 0x01, 0x01, 0x12, 0x34 };

static char hmip_set_radio_address_response[]       = { 0x06, 0x01 };
static char hmip_get_security_counter_response[]    = { 0x06, 0x01, 0x0C, 0xFF, 0xFF, 0xFF };
static char hmip_set_security_counter_response[]    = { 0x06, 0x01 };
static char hmip_set_max_sent_attemps_response[]    = { 0x06, 0x01 };
static char hmip_cmd19_response[]                   = { 0x06, 0x01, 0xE6, 0x3C, 0xD5, 0x60, 0x36, 0x4B, 0xAB, 0xEC, 0x8C, 0xC4, 0xE1, 0x2F, 0xF8, 0x19, 0x81, 0x06, 0xE5 };
static char hmip_get_nwkey_response[]               = { 0x06, 0x01, 0xC2, 0x14, 0x22, 0xF3, 0xCA, 0x9C, 0xBD, 0xA3, 0x5F, 0x71, 0x88, 0xA4, 0x73, 0xCE, 0x6F, 0x03, 0xA7, 0xA5, 0xCA, 0x26, 0xBA, 0xE8, 0xA2, 0x2A, 0x0D, 0x3D, 0x48, 0x97, 0xB6, 0xBA, 0x47, 0xF0, 0x1D, 0xCA, 0xA7, 0x39, 0xB8, 0x4D, 0xB3, 0xFB, 0x13, 0x47, 0x02, 0x15, 0xE5, 0x37, 0x52, 0xB9, 0x39, 0xDC, 0xBA, 0x22, 0x89, 0x34, 0xCA, 0x66, 0x66, 0x5B, 0xD3, 0x6F, 0xB3, 0xD5, 0x51, 0xB6, 0x67, 0x98 }; 
static char hmip_get_linkpartner_response[]         = { 0x06, 0x01 };
static char hmip_set_nwkey_response[]               = { 0x06, 0x01 };
static char hmip_add_linkpartner_response[]         = { 0x06, 0x01 };
static char hmip_send_response[]                    = { 0x06, 0x01 };

#define fake_hmrf_respond_to_frame(__frame, __response, __frame_buf, __raw_buf, __wcount) \
  __frame.cmd = __response; \
  __frame.cmdlen = sizeof(__response); \
  __wcount = encodeFrame(__frame_buf, sizeof(__frame_buf), &__frame); \
  __wcount = encodeFrameBuffer(__frame_buf, __raw_buf, __wcount); \
  fake_hmrf_add_to_buffer(__raw_buf, __wcount);

static ssize_t fake_hmrf_write(struct file *filep, const __user char *buf, size_t count, loff_t *offset)
{
  char raw_buf[1024] = {0};
  char frame_buf[1024] = {0};
  struct hm_frame frame;
  size_t raw_frame_len;

  size_t wcount = 0;

  if(count > sizeof(raw_buf))
  {
    return -EMSGSIZE;
  }

  if(copy_from_user(raw_buf, buf, count))
  {
    return -EFAULT;
  }

  raw_frame_len = decodeFrameBuffer(raw_buf, frame_buf, count);

  if(!tryParseFrame(frame_buf, raw_frame_len, &frame))
  {
    print_hex_dump(KERN_INFO, "fake_hmrf invalid frame: ", DUMP_PREFIX_NONE, 32, 1, raw_buf, count, false);
    return -EFAULT;
  }

  switch (frame.dst)
  {
    case HM_DST_COMMON:
      switch (frame.cmd[0])
      {
        case HM_COMMON_IDENTIFY:
          fake_hmrf_respond_to_frame(frame, common_identify_response, frame_buf, raw_buf, wcount);
          break;
        case HM_COMMON_GET_SGTIN:
          fake_hmrf_respond_to_frame(frame, common_get_sgtin_response, frame_buf, raw_buf, wcount);
          break;
      }
      break;

    case HM_DST_TRX:
      switch (frame.cmd[0])
      {
        case HM_TRX_GET_VERSION:
          fake_hmrf_respond_to_frame(frame, trx_get_version_response, frame_buf, raw_buf, wcount);
          break;
        case HM_TRX_GET_DUTYCYCLE:
          fake_hmrf_respond_to_frame(frame, trx_get_dutycycle_response, frame_buf, raw_buf, wcount);
          break;
        case HM_TRX_SET_DCUTYCYCLE_LIMIT:
          fake_hmrf_respond_to_frame(frame, trx_set_dutycycle_limit_response, frame_buf, raw_buf, wcount);
          break;
        case HM_TRX_GET_MCU_TYPE:
          fake_hmrf_respond_to_frame(frame, trx_get_mcu_type_response, frame_buf, raw_buf, wcount);
          break;
      }
      break;

    case HM_DST_LLMAC:
      switch (frame.cmd[0])
      {
        case HM_LLMAC_GET_TIMESTAMP:
          fake_hmrf_respond_to_frame(frame, llmac_get_timestamp_response, frame_buf, raw_buf, wcount);
          break;
        case HM_LLMAC_RFD_INIT:
          fake_hmrf_respond_to_frame(frame, llmac_rfd_init_response, frame_buf, raw_buf, wcount);
          break;
        case HM_LLMAC_GET_SERIAL:
          fake_hmrf_respond_to_frame(frame, llmac_get_serial_response, frame_buf, raw_buf, wcount);
          break;
        case HM_LLMAC_GET_DEFAULT_RF_ADDR:
          fake_hmrf_respond_to_frame(frame, llmac_get_default_rf_address_response, frame_buf, raw_buf, wcount);
          break;
      }
      break;

    case HM_DST_HMIP:
      switch (frame.cmd[0])
      {
        case HM_HMIP_SET_RADIO_ADDR:
          fake_hmrf_respond_to_frame(frame, hmip_set_radio_address_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_GET_SECURITY_COUNTER:
          fake_hmrf_respond_to_frame(frame, hmip_get_security_counter_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_SET_SECURITY_COUNTER:
          fake_hmrf_respond_to_frame(frame, hmip_set_security_counter_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_SET_MAX_SENT_ATTEMPS:
          fake_hmrf_respond_to_frame(frame, hmip_set_max_sent_attemps_response, frame_buf, raw_buf, wcount);
          break;
        case 0x19:
          fake_hmrf_respond_to_frame(frame, hmip_cmd19_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_GET_NWKEY:
          fake_hmrf_respond_to_frame(frame, hmip_get_nwkey_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_GET_LINK_PARTNER:
          fake_hmrf_respond_to_frame(frame, hmip_get_linkpartner_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_SET_NWKEY:
          fake_hmrf_respond_to_frame(frame, hmip_set_nwkey_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_ADD_LINK_PARTNER:
          fake_hmrf_respond_to_frame(frame, hmip_add_linkpartner_response, frame_buf, raw_buf, wcount);
          break;
        case HM_HMIP_SEND:
          fake_hmrf_respond_to_frame(frame, hmip_send_response, frame_buf, raw_buf, wcount);
          break;
      }
      break;
  }

  if (wcount == 0)
  {
    print_hex_dump(KERN_INFO, "fake_hmrf unsupported frame: ", DUMP_PREFIX_NONE, 32, 1, raw_buf, count, false);
  }

  return count;
}

static int fake_hmrf_open(struct inode *inode, struct file *filep)
{
  return 0;
}

static int fake_hmrf_close(struct inode *inode, struct file *filep)
{
  return 0;
}

static unsigned int fake_hmrf_poll(struct file* filep, poll_table* wait)
{
  unsigned int mask = 0;

  mask |= POLLOUT | POLLWRNORM;

  if(CIRC_CNT(fake_hmrf_rxbuf.head, fake_hmrf_rxbuf.tail, RX_BUF_SIZE) > 0)
  {
    mask |= POLLIN | POLLRDNORM;
  }
  else
  {
    poll_wait(filep, &fake_hmrf_readq, wait);

    if(CIRC_CNT(fake_hmrf_rxbuf.head, fake_hmrf_rxbuf.tail, RX_BUF_SIZE) > 0)
    {
      mask |= POLLIN | POLLRDNORM;
    }
  }

  return mask;
}

static long fake_hmrf_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
  return 0;
}

static void fake_hmrf_add_to_buffer(char *buf, size_t len)
{
  unsigned long lock_flags = 0;

  spin_lock_irqsave(&fake_hmrf_writel, lock_flags);

  while (len > 0)
  {
    if(CIRC_SPACE(fake_hmrf_rxbuf.head, fake_hmrf_rxbuf.tail, RX_BUF_SIZE))
    {
      fake_hmrf_rxbuf.buf[fake_hmrf_rxbuf.head] = *buf;
      smp_wmb();

      if(++(fake_hmrf_rxbuf.head) >= RX_BUF_SIZE)
      {
        fake_hmrf_rxbuf.head = 0;
      }
    }
    else
    {
      dev_err(fake_hmrf_dev, "rx buffer full.");
    }    

    len--;
    buf++;
  }

  spin_unlock_irqrestore(&fake_hmrf_writel, lock_flags);

  wake_up_interruptible(&fake_hmrf_readq);
}

static int fake_hmrf_get_serial(char *buffer, const struct kernel_param *kp)
{
  memcpy(buffer, &(llmac_get_serial_response[2]), 10);
  return 10;
}

static int fake_hmrf_set_serial(const char *val, const struct kernel_param *kp)
{
  if (strlen(val) != 10)
    return -EINVAL;

  memcpy(&(llmac_get_serial_response[2]), val, 10);

  return 10;
}

const struct kernel_param_ops fake_hmrf_board_serial_param_ops = 
{
  .get = &fake_hmrf_get_serial,
  .set = &fake_hmrf_set_serial,
};

module_param_cb(board_serial, &fake_hmrf_board_serial_param_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(board_serial, "Board serial number, defaults to FKE1234567.");

static int fake_hmrf_get_radio_mac(char *buffer, const struct kernel_param *kp)
{
  return sprintf(buffer, "0x%02hhX%02hhX%02hhX", llmac_get_default_rf_address_response[2], llmac_get_default_rf_address_response[3], llmac_get_default_rf_address_response[4]);
}

static int fake_hmrf_parse_hex_char(const char *val)
{
  if (*val >= 0x30 && *val <= 0x39) {
    return *val - 0x30;
  }

  if (*val >= 0x41 && *val <= 0x46) {
    return *val - 0x41 + 10;
  }

  if (*val >= 0x61 && *val <= 0x66) {
    return *val - 0x61 + 10;
  }

  return -1;
}

static int fake_hmrf_set_radio_mac(const char *val, const struct kernel_param *kp)
{
  char parsed_mac[] = { 0x0, 0x0, 0x0 };
  int parsed_char = 0;
  int i;

  if (strlen(val) != 8)
    return -EINVAL;

  if (*val++ != '0' || *val++ != 'x')
    return -EINVAL;

  for (i = 0; i < 3; i++)
  {
    parsed_char = fake_hmrf_parse_hex_char(val++);
    if (parsed_char == -1)
      return -EINVAL;

    parsed_mac[i] = (char)parsed_char << 4;

    parsed_char = fake_hmrf_parse_hex_char(val++);
    if (parsed_char == -1)
      return -EINVAL;

    parsed_mac[i] |= (char)parsed_char;
  }

  memcpy(&(llmac_get_default_rf_address_response[2]), parsed_mac, 3);

  return 8;
}

const struct kernel_param_ops fake_hmrf_radio_mac_param_ops =
{
  .get = &fake_hmrf_get_radio_mac,
  .set = &fake_hmrf_set_radio_mac,
};

module_param_cb(radio_mac, &fake_hmrf_radio_mac_param_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(radio_mac, "Radio MAC, defaults to 0x4F68F1.");

static int fake_hmrf_get_firmware_version(char *buffer, const struct kernel_param *kp)
{
  return sprintf(buffer, "%u.%u.%u", trx_get_version_response[2], trx_get_version_response[3], trx_get_version_response[4]);
}

static int fake_hmrf_set_firmware_version(const char *val, const struct kernel_param *kp)
{
  char parsed_version[] = { 0x0, 0x0, 0x0 };
  int i;
  char *token;
  int parsed_token;
  char str[32];
  char *parts = str;

  if (strlen(val) > 31)
    return -EINVAL;

  strcpy(str, val);

  token = strsep(&parts, ".");

  for (i = 0; i < 3; i++)
  {
    if (!token)
      return -EINVAL;

    parsed_token = simple_strtol(token, NULL, 10);

    if (parsed_token < 0 || parsed_token > 255)
      return -EINVAL;

    parsed_version[i] = (char) parsed_token;

    token = strsep(&parts, ".");
  }

  if (token)
    return -EINVAL;

  memcpy(&(trx_get_version_response[2]), parsed_version, 3);

  return strlen(val);
}

const struct kernel_param_ops fake_hmrf_firmware_version_param_ops =
{
  .get = &fake_hmrf_get_firmware_version,
  .set = &fake_hmrf_set_firmware_version,
};

module_param_cb(firmware_version, &fake_hmrf_firmware_version_param_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(firmware_version, "Firmware version, defaults to 2.8.6.");

static int __init fake_hmrf_init(void)
{
  int err;
  void *ptr_err;

  err = alloc_chrdev_region(&fake_hmrf_devid, 0, 1, DRIVER_NAME);
  if (err != 0)
  {
    goto failed_alloc_chrdev;
  }

  cdev_init(&fake_hmrf_cdev, &fake_hmrf_fops);
  err = cdev_add(&fake_hmrf_cdev, fake_hmrf_devid, 1);
  if (err != 0)
  {
    goto failed_cdev_add;
  }

  fake_hmrf_class = class_create(THIS_MODULE, DRIVER_NAME);
  ptr_err = fake_hmrf_class;
  if (IS_ERR(ptr_err))
    goto failed_class_create;

  fake_hmrf_dev = device_create(fake_hmrf_class, NULL, fake_hmrf_devid, NULL, DRIVER_NAME);
  ptr_err = fake_hmrf_dev;
  if (IS_ERR(ptr_err))
    goto failed_device_create;

  spin_lock_init(&fake_hmrf_writel);
  init_waitqueue_head(&fake_hmrf_readq);
  fake_hmrf_rxbuf.buf = kmalloc(RX_BUF_SIZE, GFP_KERNEL);

  return 0;

failed_device_create:
  class_destroy(fake_hmrf_class);
failed_class_create:
  cdev_del(&fake_hmrf_cdev);
  err = PTR_ERR(ptr_err);
failed_cdev_add:
  unregister_chrdev_region(fake_hmrf_devid, 1);
failed_alloc_chrdev:
  return err;
}

static void __exit fake_hmrf_exit(void)
{
  kfree(fake_hmrf_rxbuf.buf);

  device_destroy(fake_hmrf_class, fake_hmrf_devid);
  class_destroy(fake_hmrf_class);
  cdev_del(&fake_hmrf_cdev);
  unregister_chrdev_region(fake_hmrf_devid, 1);
}

module_init(fake_hmrf_init);
module_exit(fake_hmrf_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("1.3");
MODULE_DESCRIPTION("Fake HM-MOD-RPI-PCB driver");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");

