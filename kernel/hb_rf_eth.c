/*-----------------------------------------------------------------------------
 * Copyright (c) 2022 by Alexander Reinert
 * Author: Alexander Reinert
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
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <net/sock.h>
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#include <uapi/linux/sched/types.h>
#endif
#include <linux/spinlock.h>
#include <linux/circ_buf.h>
#include "generic_raw_uart.h"

#include "stack_protector.include"

#define HB_RF_ETH_PORT 3008
#define HB_RF_ETH_PROTOCOL_VERSION 2

#define TX_CHUNK_SIZE 1468

#define BUFFER_SIZE 1500

static short int autoreconnect = 1;

static struct gpio_chip gc = {0};
static spinlock_t gpio_lock;
static u8 gpio_value = 0;

static struct socket *_sock = NULL;
static struct sockaddr_in remote = {0};
static atomic_t msg_cnt = ATOMIC_INIT(0);
static struct task_struct *k_recv_thread = NULL;

static struct generic_raw_uart *raw_uart = NULL;
static struct class *class = NULL;
static struct device *dev = NULL;

static char currentEndpointIdentifier = 0;

struct send_msg_queue_entry {
  char buffer[BUFFER_SIZE];
  size_t len;
};

struct send_msg_queue_t
{
  struct send_msg_queue_entry *entries;
  int head;
  int tail;
};

static struct send_msg_queue_t *send_msg_queue;

#define QUEUE_LENGTH 32

static struct task_struct *k_send_thread = NULL;
static spinlock_t queue_write_lock;
static wait_queue_head_t queue_wq;

static uint16_t hb_rf_eth_calc_crc(unsigned char *buf, size_t len)
{
  uint16_t crc = 0xd77f;
  int i;

  while (len--)
  {
    crc ^= *buf++ << 8;
    for (i = 0; i < 8; i++)
    {
      if (crc & 0x8000)
      {
        crc <<= 1;
        crc ^= 0x8005;
      }
      else
      {
        crc <<= 1;
      }
    }
  }

  return crc;
}

static void hb_rf_eth_queue_msg(char cmd, char *buffer, size_t len)
{
  int head;
  int tail;
  struct send_msg_queue_entry *entry;

  spin_lock(&queue_write_lock);

  head = send_msg_queue->head;
  tail = READ_ONCE(send_msg_queue->tail);

  if (CIRC_SPACE(head, tail, QUEUE_LENGTH) >= 1)
  {
    entry = send_msg_queue->entries + head;

    entry->len = len + 4;
    entry->buffer[0] = cmd;
    entry->buffer[1] = 0;
    memcpy(entry->buffer + 2, buffer, len);

    smp_store_release(&send_msg_queue->head, (head + 1) & (QUEUE_LENGTH - 1));

    wake_up(&queue_wq);
  }
  else
  {
    dev_err(dev, "No free send buffers\n");
  }

  spin_unlock(&queue_write_lock);
}

static int hb_rf_eth_recv_packet(struct socket *sock, char *buffer, size_t buffer_size)
{
  struct kvec vec = {0};
  struct msghdr msg = {0};
  int len;

  if (sock == NULL)
    return -EPROTO;

  vec.iov_len = buffer_size;
  vec.iov_base = buffer;

  len = kernel_recvmsg(sock, &msg, &vec, 1, buffer_size, 0);

  if (len > 0)
  {
    if (len < 4)
    {
      dev_err(dev, "Received to small UDP packet\n");
      return -EPROTO;
    }
    if (*((uint16_t *)(buffer + len - 2)) != (uint16_t)(htons(hb_rf_eth_calc_crc(buffer, len - 2))))
    {
      dev_err(dev, "Received UDP packet with invalid checksum\n");
      return -EPROTO;
    }
  }
  else if (len != 0 && len != -EAGAIN)
  {
    dev_err(dev, "Error %d on receiving packet\n", len);
  }

  return len;
}

static void hb_rf_eth_set_timeout(struct socket *sock)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
  #define MY_SO_RCVTIMEO SO_RCVTIMEO_NEW
  struct __kernel_sock_timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
#else
  #define MY_SO_RCVTIMEO SO_RCVTIMEO
  struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
  sock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO_NEW, KERNEL_SOCKPTR((char *)&tv), sizeof(tv));
#else
  mm_segment_t fs = get_fs();
  set_fs(KERNEL_DS);
  sock_setsockopt(sock, SOL_SOCKET, MY_SO_RCVTIMEO, (char *)&tv, sizeof(tv));
  set_fs(fs);
#endif
}

static void hb_rf_eth_send_msg(struct socket *sock, char *buffer, size_t len)
{
  struct kvec vec = {0};
  struct msghdr header = {0};
  int err;

  *((uint8_t *)(buffer + 1)) = (uint8_t)(atomic_inc_return(&msg_cnt));
  *((uint16_t *)(buffer + len - 2)) = (uint16_t)(htons(hb_rf_eth_calc_crc(buffer, len - 2)));

  if (sock)
  {
    vec.iov_len = len;
    vec.iov_base = buffer;

    header.msg_name = &remote;
    header.msg_namelen = sizeof(struct sockaddr_in);
    header.msg_control = NULL;
    header.msg_controllen = 0;
    header.msg_flags = 0;

    err = kernel_sendmsg(sock, &header, &vec, 1, len);

    if (err < 0)
    {
      dev_err(dev, "Error %d on sending packet\n", err);
    }
    else if (err != len)
    {
      dev_err(dev, "Only %d of %d bytes of packet could be sent\n", err, (int)len);
    }
  }
  else
  {
    dev_err(dev, "Error sending packet, not connected\n");
  }
}

static int hb_rf_eth_try_connect(char endpointIdentifier)
{
  int err;
  char buffer[6] = {0, 0, HB_RF_ETH_PROTOCOL_VERSION, endpointIdentifier, 0, 0};
  unsigned long timeout;
  char *recv_buffer;
  int len;
  struct socket *sock;

  err = sock_create_kern(&init_net, AF_INET, SOCK_DGRAM, IPPROTO_UDP, &sock);

  if (err < 0)
  {
    dev_err(dev, "Error %d while creating socket\n", err);
    return err;
  }

  hb_rf_eth_set_timeout(sock);

  err = sock->ops->connect(sock, (struct sockaddr *)&remote, sizeof(remote), 0);
  if (err < 0)
  {
    dev_err(dev, "Error %d while connecting to %pI4\n", err, &remote.sin_addr);
    sock_release(sock);
    return err;
  }
  else
  {
    hb_rf_eth_send_msg(sock, buffer, sizeof(buffer));

    err = -ETIMEDOUT;
    recv_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    timeout = jiffies + msecs_to_jiffies(50);
    while (time_before(jiffies, timeout))
    {
      len = hb_rf_eth_recv_packet(sock, recv_buffer, BUFFER_SIZE);
      if (len == 7)
      {
        if (recv_buffer[0] == 0 && recv_buffer[2] == HB_RF_ETH_PROTOCOL_VERSION && recv_buffer[3] == buffer[1])
        {
          currentEndpointIdentifier = recv_buffer[4];
          err = 0;
          break;
        }
      }
    }

    if (err)
    {
      dev_err(dev, "Timeout occured while connecting to %pI4\n", &remote.sin_addr);
      sock_release(sock);
      return err;
    }
  }

  _sock = sock;
  sysfs_notify(&dev->kobj, NULL, "is_connected");
  generic_raw_uart_set_connection_state(raw_uart, true);
  return 0;
}

static void hb_rf_eth_set_high_prio(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
  sched_set_fifo(current);
#else
  int err;
  struct sched_param param;
  param.sched_priority = 5;
  err = sched_setscheduler(current, SCHED_RR, &param);
  if (err < 0)
    dev_err(dev, "Error setting priority of thread (err %d).\n", err);
#endif
}

static bool is_queue_filled(int *head, int *tail)
{
  *head = smp_load_acquire(&send_msg_queue->head);
  *tail = send_msg_queue->tail;

  return CIRC_CNT(*head, *tail, QUEUE_LENGTH) >= 1;
}

static int hb_rf_eth_send_threadproc(void *data)
{
  struct send_msg_queue_entry *entry;
  char buffer[4] = {2, 0, 0, 0};
  int head;
  int tail;
  unsigned long nextKeepAliveSentOut = jiffies;

  hb_rf_eth_set_high_prio();

  while (!kthread_should_stop())
  {
    if (is_queue_filled(&head, &tail) || wait_event_interruptible_timeout(queue_wq, is_queue_filled(&head, &tail), msecs_to_jiffies(100)) > 0)
    {
      entry = send_msg_queue->entries + tail;

      if (entry->buffer[0] == 3)
      {
        mb();
        entry->buffer[2] = gpio_value;
      }

      hb_rf_eth_send_msg(_sock, entry->buffer, entry->len);

      tail = (tail + 1) & (QUEUE_LENGTH - 1);
      smp_store_release(&send_msg_queue->tail, tail);
    }

    if (time_after(jiffies, nextKeepAliveSentOut))
    {
      nextKeepAliveSentOut = jiffies + msecs_to_jiffies(1000);
      hb_rf_eth_send_msg(_sock, buffer, 4);
    }
  }

  return 0;
}

static int hb_rf_eth_recv_threadproc(void *data)
{
  char *buffer;
  int len;
  int i;
  unsigned long lastReceivedKeepAlive = jiffies;

  hb_rf_eth_set_high_prio();

  buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);

  while (!kthread_should_stop())
  {
    len = hb_rf_eth_recv_packet(_sock, buffer, BUFFER_SIZE);
    if (len >= 4)
    {
      switch (buffer[0])
      {
      case 2:
        lastReceivedKeepAlive = jiffies;
        break;
      case 7:
        lastReceivedKeepAlive = jiffies;
        for (i = 2; i < len - 2; i++)
        {
          generic_raw_uart_handle_rx_char(raw_uart, GENERIC_RAW_UART_RX_STATE_NONE, (unsigned char)buffer[i]);
        }
        generic_raw_uart_rx_completed(raw_uart);
        break;
      default:
        print_hex_dump(KERN_INFO, "Received unknown UDP packet: ", DUMP_PREFIX_NONE, 16, 1, buffer, len, false);
        break;
      }
    }

    if (time_after(jiffies, lastReceivedKeepAlive + msecs_to_jiffies(5000)))
    {
      dev_err(dev, "Did not receive any packet in the last 5 seconds, terminating connection.\n");
      sock_release(_sock);
      _sock = NULL;
      sysfs_notify(&dev->kobj, NULL, "is_connected");
      generic_raw_uart_set_connection_state(raw_uart, false);

      if (autoreconnect)
      {
        while (!kthread_should_stop())
        {
          dev_info(dev, "Trying to reconnect to %pI4\n", &remote.sin_addr);
          if (hb_rf_eth_try_connect(currentEndpointIdentifier) == 0)
          {
            lastReceivedKeepAlive = jiffies;
            dev_info(dev, "Successfully connected to %pI4\n", &remote.sin_addr);
            break;
          }
	  msleep_interruptible(500);
        }
	continue;
      }
      else
      {
        goto exit;
      }
    }
  }

exit:
  k_recv_thread = NULL;
  kfree(buffer);
  return 0;
}

static void hb_rf_eth_send_reset(void)
{
  hb_rf_eth_queue_msg(4, NULL, 0);
  msleep(100);
}

static int hb_rf_eth_connect(const char *ip)
{
  int err;
  __be32 addr;

  if (ip[0] == 0)
  {
    dev_err(dev, "Failed to load module, no remote ip was given.\n");
    return -EINVAL;
  }

  addr = in_aton(ip);
  dev_info(dev, "Trying to connect to %pI4\n", &addr);

  remote.sin_addr.s_addr = addr;
  remote.sin_family = AF_INET;
  remote.sin_port = htons(HB_RF_ETH_PORT);

  err = hb_rf_eth_try_connect(0);
  if (err != 0)
  {
    return err;
  }

  k_recv_thread = kthread_run(hb_rf_eth_recv_threadproc, NULL, "k_hb_rf_eth_receiver");
  if (IS_ERR(k_recv_thread))
  {
    err = PTR_ERR(k_recv_thread);
    dev_err(dev, "Error creating receiver thread\n");
    k_recv_thread = NULL;
    sock_release(_sock);
    _sock = NULL;
    sysfs_notify(&dev->kobj, NULL, "is_connected");
    generic_raw_uart_set_connection_state(raw_uart, false);
    return err;
  }
  else
  {
    k_send_thread = kthread_run(hb_rf_eth_send_threadproc, NULL, "k_hb_rf_eth_sender");
    if (IS_ERR(k_send_thread))
    {
      err = PTR_ERR(k_send_thread);
      dev_err(dev, "Error creating sender thread\n");
      k_send_thread = NULL;
      kthread_stop(k_recv_thread);
      k_recv_thread = NULL;
      sock_release(_sock);
      _sock = NULL;
      sysfs_notify(&dev->kobj, NULL, "is_connected");
      generic_raw_uart_set_connection_state(raw_uart, false);
      return err;
    }
  }

  hb_rf_eth_send_reset();

  dev_info(dev, "Successfully connected to %pI4\n", &remote.sin_addr);

  return 0;
}

static void hb_rf_eth_disconnect(void)
{
  char buffer[4] = {1, 0, 0, 0};

  if (k_send_thread)
  {
    kthread_stop(k_send_thread);
    k_send_thread = NULL;
  }
  if (k_recv_thread)
  {
    kthread_stop(k_recv_thread);
    k_recv_thread = NULL;
  }

  if (_sock)
  {
    hb_rf_eth_send_msg(_sock, buffer, sizeof(buffer));
    sock_release(_sock);
    _sock = NULL;
    sysfs_notify(&dev->kobj, NULL, "is_connected");
    generic_raw_uart_set_connection_state(raw_uart, false);
  }
}

static void hb_rf_eth_send_gpio(void)
{
  mb();
  hb_rf_eth_queue_msg(3, &gpio_value, 1);
}

static int hb_rf_eth_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
  if (offset > 2)
    return -ENODEV;

  if (gc->owner && !try_module_get(gc->owner))
    return -ENODEV;

  return 0;
}

static void hb_rf_eth_gpio_free(struct gpio_chip *gc, unsigned offset)
{
  module_put(gc->owner);
}

static int hb_rf_eth_gpio_direction_get(struct gpio_chip *gc, unsigned int gpio)
{
  return 0;
}

static int hb_rf_eth_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
  return -EPERM;
}

static int hb_rf_eth_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio, int value)
{
  return 0;
}

static int hb_rf_eth_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
  return gpio_value & BIT(gpio);
}

static void hb_rf_eth_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
  unsigned long lock_flags;

  spin_lock_irqsave(&gpio_lock, lock_flags);

  if (value)
    gpio_value |= BIT(gpio);
  else
    gpio_value &= ~BIT(gpio);

  hb_rf_eth_send_gpio();

  spin_unlock_irqrestore(&gpio_lock, lock_flags);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
static int hb_rf_eth_gpio_get_multiple(struct gpio_chip *gc, unsigned long *mask, unsigned long *bits)
{
  *bits = gpio_value & *mask;

  return 0;
}
#endif

static void hb_rf_eth_gpio_set_multiple(struct gpio_chip *gc, unsigned long *mask, unsigned long *bits)
{
  unsigned long lock_flags;

  spin_lock_irqsave(&gpio_lock, lock_flags);

  gpio_value &= ~(*mask);
  gpio_value |= *bits & *mask;

  hb_rf_eth_send_gpio();

  spin_unlock_irqrestore(&gpio_lock, lock_flags);
}

static int hb_rf_eth_get_gpio_pin_number(struct generic_raw_uart *raw_uart, enum generic_raw_uart_pin pin)
{
  switch (pin)
  {
  case GENERIC_RAW_UART_PIN_RED:
    return gc.base;
  case GENERIC_RAW_UART_PIN_GREEN:
    return gc.base + 1;
  case GENERIC_RAW_UART_PIN_BLUE:
    return gc.base + 2;
  case GENERIC_RAW_UART_PIN_RESET:
  case GENERIC_RAW_UART_PIN_ALT_RESET:
    return 0;
  }
  return 0;
}

static int hb_rf_eth_reset_radio_module(struct generic_raw_uart *raw_uart)
{
  hb_rf_eth_send_reset();
  return 0;
}

static int hb_rf_eth_start_connection(struct generic_raw_uart *raw_uart)
{
  hb_rf_eth_queue_msg(5, NULL, 0);
  msleep(20);
  return 0;
}

static void hb_rf_eth_stop_connection(struct generic_raw_uart *raw_uart)
{
  hb_rf_eth_queue_msg(6, NULL, 0);
  msleep(20);
}

static void hb_rf_eth_stop_tx(struct generic_raw_uart *raw_uart)
{
  // nothing to do
}

static bool hb_rf_eth_isready_for_tx(struct generic_raw_uart *raw_uart)
{
  return true;
}

static void hb_rf_eth_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len)
{
  hb_rf_eth_queue_msg(7, chr + index, len);
}

static void hb_rf_eth_init_tx(struct generic_raw_uart *raw_uart)
{
  // nothing to do
}

static int hb_rf_eth_get_device_type(struct generic_raw_uart *raw_uart, char *page)
{
  if (_sock != NULL)
  {
    return snprintf(page, MAX_DEVICE_TYPE_LEN, "HB-RF-ETH@%pI4", &remote.sin_addr);
  }
  else
  {
    return snprintf(page, MAX_DEVICE_TYPE_LEN, "HB-RF-ETH@-");
  }
}

static struct raw_uart_driver hb_rf_eth = {
    .owner = THIS_MODULE,
    .get_gpio_pin_number = hb_rf_eth_get_gpio_pin_number,
    .reset_radio_module = hb_rf_eth_reset_radio_module,
    .start_connection = hb_rf_eth_start_connection,
    .stop_connection = hb_rf_eth_stop_connection,
    .init_tx = hb_rf_eth_init_tx,
    .isready_for_tx = hb_rf_eth_isready_for_tx,
    .tx_chars = hb_rf_eth_tx_chars,
    .stop_tx = hb_rf_eth_stop_tx,
    .get_device_type = hb_rf_eth_get_device_type,
    .tx_chunk_size = TX_CHUNK_SIZE,
    .tx_bulktransfer_size = TX_CHUNK_SIZE,
};

static ssize_t connect_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  int err;
  char ip[17];

  if (count == 0)
    return 0;

  if (count > 16)
    return -EINVAL;

  hb_rf_eth_disconnect();

  if (buf[0] == 0 || buf[0] == '-')
  {
    return count;
  }

  memcpy(ip, buf, count);
  ip[count] = 0;

  err = hb_rf_eth_connect(ip);

  return err == 0 ? count : err;
}
static DEVICE_ATTR_WO(connect);

static ssize_t is_connected_show(struct device *dev, struct device_attribute *attr, char *page)
{
  return sprintf(page, "%d\n", _sock != NULL ? 1 : 0);
}
static DEVICE_ATTR_RO(is_connected);

static int __init hb_rf_eth_init(void)
{
  int err;

  spin_lock_init(&gpio_lock);

  spin_lock_init(&queue_write_lock);
  init_waitqueue_head(&queue_wq);

  send_msg_queue = kzalloc(sizeof(struct send_msg_queue_t), GFP_KERNEL);
  send_msg_queue->entries = kcalloc(QUEUE_LENGTH, sizeof(struct send_msg_queue_entry), GFP_KERNEL);

  class = class_create(THIS_MODULE, "hb-rf-eth");
  if (IS_ERR(class))
  {
    err = PTR_ERR(class);
    goto failed_class_create;
  }

  dev = device_create(class, NULL, 0, NULL, "hb-rf-eth");
  if (IS_ERR(dev))
  {
    err = PTR_ERR(dev);
    goto failed_dev_create;
  }

  gpio_value = 0;

  gc.label = "hb-rf-eth-gpio";
  gc.ngpio = 3;
  gc.request = hb_rf_eth_gpio_request;
  gc.free = hb_rf_eth_gpio_free;
  gc.get_direction = hb_rf_eth_gpio_direction_get;
  gc.direction_input = hb_rf_eth_gpio_direction_input;
  gc.direction_output = hb_rf_eth_gpio_direction_output;
  gc.get = hb_rf_eth_gpio_get;
  gc.set = hb_rf_eth_gpio_set;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
  gc.get_multiple = hb_rf_eth_gpio_get_multiple;
#endif
  gc.set_multiple = hb_rf_eth_gpio_set_multiple;
  gc.owner = THIS_MODULE;
  gc.parent = dev;
  gc.base = -1;
  gc.can_sleep = false;

  err = gpiochip_add(&gc);
  if (err)
    goto failed_gc_create;

  raw_uart = generic_raw_uart_probe(dev, &hb_rf_eth, NULL);
  if (IS_ERR(raw_uart))
  {
    err = PTR_ERR(dev);
    goto failed_raw_uart_probe;
  }

  generic_raw_uart_set_connection_state(raw_uart, false);

  sysfs_create_file(&dev->kobj, &dev_attr_is_connected.attr);
  sysfs_create_file(&dev->kobj, &dev_attr_connect.attr);

  return 0;

failed_raw_uart_probe:
  gpiochip_remove(&gc);
failed_gc_create:
  device_destroy(class, 0);
failed_dev_create:
  class_destroy(class);
failed_class_create:
  return err;
}

static void __exit hb_rf_eth_exit(void)
{
  if (raw_uart)
    generic_raw_uart_remove(raw_uart);

  sysfs_remove_file(&dev->kobj, &dev_attr_is_connected.attr);
  sysfs_remove_file(&dev->kobj, &dev_attr_connect.attr);

  gpiochip_remove(&gc);

  hb_rf_eth_disconnect();

  device_destroy(class, 0);
  class_destroy(class);

  kfree(send_msg_queue->entries);
  kfree(send_msg_queue);
}

static int hb_rf_eth_connect_set(const char *val, const struct kernel_param *kp)
{
  hb_rf_eth_disconnect();

  if (val == NULL || val[0] == 0 || val[0] == '-')
  {
    return 0;
  }

  return hb_rf_eth_connect(val);
}

static const struct kernel_param_ops hb_rf_eth_connect_param_ops = {
    .set = hb_rf_eth_connect_set,
};

module_param_cb(connect, &hb_rf_eth_connect_param_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(connect, "Deprecated! Use /sys/class/hb-rf-eth/hb-rf-eth/connect instead.");

module_param(autoreconnect, short, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(autoreconnect, "If enabled, the module will automatically try to reconnect");

module_init(hb_rf_eth_init);
module_exit(hb_rf_eth_exit);

MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
MODULE_DESCRIPTION("HB-RF-ETH raw uart driver");
MODULE_VERSION("1.19");
MODULE_LICENSE("GPL");
