/*-----------------------------------------------------------------------------
 * Copyright (c) 2020 by Alexander Reinert
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
#include <linux/workqueue.h>
#include "generic_raw_uart.h"

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

static unsigned long nextKeepAliveSentOut = 0;
static unsigned long lastReceivedKeepAlive = 0;

static char currentEndpointIdentifier = 0;

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

static void hb_rf_eth_send_msg(struct socket *sock, char *buffer, size_t len)
{
  struct kvec vec = {0};
  mm_segment_t oldmm;
  struct msghdr header = {0};

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

    oldmm = get_fs();
    set_fs(KERNEL_DS);
    len = kernel_sendmsg(sock, &header, &vec, 1, len);
    set_fs(oldmm);

    if (len < 0)
      dev_err(dev, "Error %d on sending packet\n", (int)len);
  }
  else
  {
    dev_err(dev, "Error sending packet, not connected\n");
  }
}

static int hb_rf_eth_recv_packet(struct socket *sock, char *buffer, size_t buffer_size)
{
  struct kvec vec = {0};
  mm_segment_t oldmm;
  struct msghdr msg = {0};
  int len;

  if (sock == NULL)
    return -EPROTO;

  vec.iov_len = buffer_size;
  vec.iov_base = buffer;

  oldmm = get_fs();
  set_fs(KERNEL_DS);
  len = kernel_recvmsg(sock, &msg, &vec, 1, buffer_size, 0);
  set_fs(oldmm);

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
  struct __kernel_sock_timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
  #define MY_SO_RCVTIMEO SO_RCVTIMEO_NEW
#else
  struct timeval tv = { .tv_sec = 0, .tv_usec = 100000 };
  #define MY_SO_RCVTIMEO SO_RCVTIMEO
#endif

  mm_segment_t fs = get_fs();
  set_fs(KERNEL_DS);
  sock_setsockopt(sock, SOL_SOCKET, MY_SO_RCVTIMEO, (char *)&tv, sizeof(tv));
  set_fs(fs);
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
  return 0;
}

static int hb_rf_eth_recv_threadproc(void *data)
{
  char *buffer;
  int len;
  int i;

  buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);

  lastReceivedKeepAlive = jiffies;

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

    if (time_after(jiffies, nextKeepAliveSentOut))
    {
      nextKeepAliveSentOut = jiffies + msecs_to_jiffies(1000);
      buffer[0] = 2;
      hb_rf_eth_send_msg(_sock, buffer, 4);
    }
  }

exit:
  k_recv_thread = NULL;
  kfree(buffer);
  return 0;
}

static void hb_rf_eth_send_reset(void)
{
  char buffer[4] = {4, 0, 0, 0};
  hb_rf_eth_send_msg(_sock, buffer, sizeof(buffer));
  msleep(100);
}

static int hb_rf_eth_connect(const char *ip)
{
  int err;

  if (ip[0] == 0)
  {
    dev_err(dev, "Failed to load module, no remote ip was given.\n");
    return -EINVAL;
  }

  dev_info(dev, "Trying to connect to %s\n", ip);

  remote.sin_addr.s_addr = in_aton(ip);
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
    return err;
  }

  hb_rf_eth_send_reset();

  dev_info(dev, "Successfully connected to %pI4\n", &remote.sin_addr);

  return 0;
}

static void hb_rf_eth_disconnect(void)
{
  char buffer[4] = {1, 0, 0, 0};

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
  }
}

static void hb_rf_eth_send_gpio(struct work_struct *work)
{
  char buffer[5] = {3, 0, gpio_value, 0, 0};
  hb_rf_eth_send_msg(_sock, buffer, sizeof(buffer));
}

static DECLARE_WORK(hb_rf_eth_send_gpio_work, hb_rf_eth_send_gpio);

static int hb_rf_eth_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
  if (offset > 2)
    return -ENODEV;

  return 0;
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

  queue_work(system_highpri_wq, &hb_rf_eth_send_gpio_work);

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

  queue_work(system_highpri_wq, &hb_rf_eth_send_gpio_work);

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
    return 0;
  }
  return 0;
}

static void hb_rf_eth_reset_radio_module(struct generic_raw_uart *raw_uart)
{
  hb_rf_eth_send_reset();
}

static int hb_rf_eth_start_connection(struct generic_raw_uart *raw_uart)
{
  char buffer[4] = {5, 0, 0, 0};
  hb_rf_eth_send_msg(_sock, buffer, sizeof(buffer));
  msleep(20);
  return 0;
}

static void hb_rf_eth_stop_connection(struct generic_raw_uart *raw_uart)
{
  char buffer[4] = {6, 0, 0, 0};
  hb_rf_eth_send_msg(_sock, buffer, sizeof(buffer));
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

static unsigned char tx_char_buffer[BUFFER_SIZE];
static void hb_rf_eth_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len)
{
  tx_char_buffer[0] = 7;
  memcpy(&tx_char_buffer[2], &chr[index], len);
  hb_rf_eth_send_msg(_sock, tx_char_buffer, len + 4);
}

static void hb_rf_eth_init_tx(struct generic_raw_uart *raw_uart)
{
  // nothing to do
}

static bool hb_rf_eth_is_connected(struct generic_raw_uart *raw_uart)
{
  return _sock != NULL;
}

static struct raw_uart_driver hb_rf_eth = {
    .get_gpio_pin_number = hb_rf_eth_get_gpio_pin_number,
    .reset_radio_module = hb_rf_eth_reset_radio_module,
    .start_connection = hb_rf_eth_start_connection,
    .stop_connection = hb_rf_eth_stop_connection,
    .init_tx = hb_rf_eth_init_tx,
    .isready_for_tx = hb_rf_eth_isready_for_tx,
    .tx_chars = hb_rf_eth_tx_chars,
    .stop_tx = hb_rf_eth_stop_tx,
    .tx_chunk_size = TX_CHUNK_SIZE,
    .tx_bulktransfer_size = TX_CHUNK_SIZE,
    .is_connected = hb_rf_eth_is_connected,
};

static int __init hb_rf_eth_init(void)
{
  int err;

  spin_lock_init(&gpio_lock);

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
    generic_raw_uart_remove(raw_uart, dev, &hb_rf_eth);

  cancel_work_sync(&hb_rf_eth_send_gpio_work);

  gpiochip_remove(&gc);

  hb_rf_eth_disconnect();

  device_destroy(class, 0);
  class_destroy(class);
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
MODULE_PARM_DESC(connect, "Connects the module to a HB-RF-ETH pcb");

module_param(autoreconnect, short, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(autoreconnect, "If enabled, the module will automatically try to reconnect");

module_init(hb_rf_eth_init);
module_exit(hb_rf_eth_exit);

MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
MODULE_DESCRIPTION("HB-RF-ETH raw uart driver");
MODULE_VERSION("1.4");
MODULE_LICENSE("GPL");
