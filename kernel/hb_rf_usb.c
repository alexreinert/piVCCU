/*-----------------------------------------------------------------------------
 * Copyright (c) 2018 by Alexander Reinert
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
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>
#include <linux/version.h>
#include "generic_raw_uart.h"

#define WDR_TIMEOUT 5000 /* default urb timeout */
#define WDR_SHORT_TIMEOUT 1000	/* shorter urb timeout */

#define FTDI_SIO_RESET_REQUEST_TYPE 0x40
#define FTDI_SIO_RESET_REQUEST 0
#define FTDI_SIO_RESET_SIO 0

#define FTDI_SIO_SET_BITMODE_REQUEST_TYPE 0x40
#define FTDI_SIO_SET_BITMODE_REQUEST 0x0b

#define FTDI_SIO_GET_MODEM_STATUS_REQUEST_TYPE 0xc0
#define FTDI_SIO_GET_MODEM_STATUS_REQUEST 0x05

#define FTDI_SIO_SET_EVENT_CHAR_REQUEST_TYPE 0x40
#define FTDI_SIO_SET_EVENT_CHAR_REQUEST 0x06

#define FTDI_SIO_SET_ERROR_CHAR_REQUEST_TYPE 0x40
#define FTDI_SIO_SET_ERROR_CHAR_REQUEST 0x07

#define FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE 0x40
#define FTDI_SIO_SET_BAUDRATE_REQUEST  0x03

#define FTDI_SIO_SET_LATENCY_TIMER_REQUEST_TYPE 0x40
#define FTDI_SIO_SET_LATENCY_TIMER_REQUEST 0x09

#define FTDI_SIO_SET_DATA_REQUEST_TYPE 0x40
#define FTDI_SIO_SET_DATA_REQUEST 0x04

#define FTDI_SIO_SET_DATA_PARITY_NONE (0x0 << 8)
#define FTDI_SIO_SET_DATA_STOP_BITS_1 (0x0 << 11)

#define FTDI_RS_OE (1<<1)
#define FTDI_RS_PE (1<<2)
#define FTDI_RS_FE (1<<3)
#define FTDI_RS_BI (1<<4)
#define FTDI_RS_TEMT (1<<6)

#define FTDI_SIO_BITMODE_RESET 0x00
#define FTDI_SIO_BITMODE_CBUS 0x20

#define TX_CHUNK_SIZE 11

#define BUFFER_SIZE 256

struct hb_rf_usb_port_s
{
  struct generic_raw_uart *raw_uart;

  struct urb *write_urb;
  unsigned char *write_buffer;
  bool is_in_tx;
  spinlock_t is_in_tx_lock;

  struct urb *read_urb;
  unsigned char *read_buffer;

  struct usb_device *udev;
  struct gpio_chip gc;

  spinlock_t gpio_lock;
  u8 gpio_value;
  u8 gpio_direction;
};

static struct usb_device_id usbid [] = {
  { USB_DEVICE(0x0403, 0x6F70), },
  { }
};

MODULE_DEVICE_TABLE(usb, usbid);

static int hb_rf_usb_set_bitmode(struct hb_rf_usb_port_s *port, u8 mode)
{
  int result;
  u16 val;

  val = (mode << 8) | (port->gpio_direction << 4) | port->gpio_value;
  result = usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), FTDI_SIO_SET_BITMODE_REQUEST, FTDI_SIO_SET_BITMODE_REQUEST_TYPE, val, 0, NULL, 0, WDR_TIMEOUT);
  if (result < 0) {
    dev_err(&port->udev->dev, "bitmode request failed for value 0x%04x: %d\n", val, result);
  }

  return result;
}

static void hb_rf_usb_set_cbus_pins_completion(struct urb *urb)
{
  kfree(urb->setup_packet);
  usb_free_urb(urb);
}

static void hb_rf_usb_set_cbus_pins(struct hb_rf_usb_port_s *port)
{
  struct usb_ctrlrequest *dr;
  struct urb *urb;

  dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);

  dr->bRequestType = FTDI_SIO_SET_BITMODE_REQUEST_TYPE;
  dr->bRequest = FTDI_SIO_SET_BITMODE_REQUEST;
  dr->wValue = cpu_to_le16((FTDI_SIO_BITMODE_CBUS << 8) | (port->gpio_direction << 4) | port->gpio_value);
  dr->wIndex = 0;
  dr->wLength = 0;

  urb = usb_alloc_urb(0, GFP_ATOMIC);

  usb_fill_control_urb(urb, port->udev, usb_sndctrlpipe(port->udev, 0), (void*)dr, NULL, 0, hb_rf_usb_set_cbus_pins_completion, NULL);

  usb_submit_urb(urb, GFP_ATOMIC);
}

static int hb_rf_usb_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
  if (offset > 3)
    return -ENODEV;

  return 0;
}

static int hb_rf_usb_gpio_direction_get(struct gpio_chip *gc, unsigned int gpio)
{
  struct hb_rf_usb_port_s *port = container_of(gc, struct hb_rf_usb_port_s, gc);

  return !(port->gpio_direction & BIT(gpio));
}

static int hb_rf_usb_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
  struct hb_rf_usb_port_s *port = container_of(gc, struct hb_rf_usb_port_s, gc);
  unsigned long lock_flags;

  spin_lock_irqsave(&port->gpio_lock, lock_flags);

  port->gpio_direction &= ~BIT(gpio);

  hb_rf_usb_set_cbus_pins(port);

  spin_unlock_irqrestore(&port->gpio_lock, lock_flags);

  return 0;
}

static int hb_rf_usb_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio, int value)
{
  struct hb_rf_usb_port_s *port = container_of(gc, struct hb_rf_usb_port_s, gc);
  unsigned long lock_flags;

  spin_lock_irqsave(&port->gpio_lock, lock_flags);

  port->gpio_direction |= BIT(gpio);

  if (value)
    port->gpio_value |= BIT(gpio);
  else
    port->gpio_value &= ~BIT(gpio);

  hb_rf_usb_set_cbus_pins(port);

  spin_unlock_irqrestore(&port->gpio_lock, lock_flags);

  return 0;
}

static int hb_rf_usb_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
  struct hb_rf_usb_port_s *port = container_of(gc, struct hb_rf_usb_port_s, gc);

  return port->gpio_value & BIT(gpio);
}

static void hb_rf_usb_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
  struct hb_rf_usb_port_s *port = container_of(gc, struct hb_rf_usb_port_s, gc);
  unsigned long lock_flags;

  spin_lock_irqsave(&port->gpio_lock, lock_flags);

  if (value)
    port->gpio_value |= BIT(gpio);
  else
    port->gpio_value &= ~BIT(gpio);

  hb_rf_usb_set_cbus_pins(port);

  spin_unlock_irqrestore(&port->gpio_lock, lock_flags);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
static int hb_rf_usb_gpio_get_multiple(struct gpio_chip *gc, unsigned long *mask, unsigned long *bits)
{
  struct hb_rf_usb_port_s *port = container_of(gc, struct hb_rf_usb_port_s, gc);

  *bits = port->gpio_value & *mask;

  return 0;
}
#endif

static void hb_rf_usb_gpio_set_multiple(struct gpio_chip *gc, unsigned long *mask, unsigned long *bits)
{
  struct hb_rf_usb_port_s *port = container_of(gc, struct hb_rf_usb_port_s, gc);
  unsigned long lock_flags;

  spin_lock_irqsave(&port->gpio_lock, lock_flags);

  port->gpio_value &= ~(*mask);
  port->gpio_value |= *bits & *mask;

  hb_rf_usb_set_cbus_pins(port);

  spin_unlock_irqrestore(&port->gpio_lock, lock_flags);
}

static int hb_rf_usb_start_connection(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_stop_connection(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_stop_tx(struct generic_raw_uart *raw_uart);
static bool hb_rf_usb_isready_for_tx(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len);
static void hb_rf_usb_init_tx(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_process_read_urb(struct urb *urb);
static void hb_rf_usb_process_write_urb(struct urb *urb);

static void hb_rf_usb_init_uart(struct hb_rf_usb_port_s *port)
{
  // reset uart
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), FTDI_SIO_RESET_REQUEST, FTDI_SIO_RESET_REQUEST_TYPE, FTDI_SIO_RESET_SIO, 0, NULL, 0, WDR_TIMEOUT);
  msleep(50);
 
  // set baudrate (115200)
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), FTDI_SIO_SET_BAUDRATE_REQUEST, FTDI_SIO_SET_BAUDRATE_REQUEST_TYPE, 26, 0, NULL, 0, WDR_SHORT_TIMEOUT);

  // set 8N1
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), FTDI_SIO_SET_DATA_REQUEST, FTDI_SIO_SET_DATA_REQUEST_TYPE, 8 | FTDI_SIO_SET_DATA_PARITY_NONE | FTDI_SIO_SET_DATA_STOP_BITS_1, 0, NULL, 0, WDR_SHORT_TIMEOUT);

  // disable any character replacement
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), FTDI_SIO_SET_EVENT_CHAR_REQUEST, FTDI_SIO_SET_EVENT_CHAR_REQUEST_TYPE, 0, 0, NULL, 0, WDR_SHORT_TIMEOUT);
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), FTDI_SIO_SET_ERROR_CHAR_REQUEST, FTDI_SIO_SET_ERROR_CHAR_REQUEST_TYPE, 0, 0, NULL, 0, WDR_SHORT_TIMEOUT);

  // set read timeout
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), FTDI_SIO_SET_LATENCY_TIMER_REQUEST, FTDI_SIO_SET_LATENCY_TIMER_REQUEST_TYPE, 1, 0, NULL, 0, WDR_SHORT_TIMEOUT);

  port->write_urb = usb_alloc_urb(0, GFP_KERNEL);
  port->write_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
  usb_fill_bulk_urb(port->write_urb, port->udev, usb_sndbulkpipe(port->udev, 0x02), port->write_buffer, BUFFER_SIZE, hb_rf_usb_process_write_urb, port);

  port->read_urb = usb_alloc_urb(0, GFP_KERNEL);
  port->read_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
  usb_fill_bulk_urb(port->read_urb, port->udev, usb_rcvbulkpipe(port->udev, 0x81), port->read_buffer, BUFFER_SIZE, hb_rf_usb_process_read_urb, port);
}

static void hb_rf_usb_process_read_urb(struct urb *urb)
{
  struct hb_rf_usb_port_s *port = urb->context;

  enum generic_raw_uart_rx_flags flags = GENERIC_RAW_UART_RX_STATE_NONE;
  char *data = (char *)urb->transfer_buffer;
  int status = data[1];
  int i;

  /* Error handling */
  if(status & FTDI_RS_BI)
  {
    generic_raw_uart_handle_rx_char(port->raw_uart, GENERIC_RAW_UART_RX_STATE_BREAK, 0);
  }
  else
  {
    if(status & FTDI_RS_PE)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_PARITY;
    }
    if(status & FTDI_RS_FE)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_FRAME;
    }
    if(status & FTDI_RS_OE)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_OVERRUN;
    }
  }

  if (urb->actual_length > 2)
  {
    generic_raw_uart_handle_rx_char(port->raw_uart, flags, (unsigned char)data[2]);
  }

  for (i = 3; i < urb->actual_length; i++)
  {
    generic_raw_uart_handle_rx_char(port->raw_uart, GENERIC_RAW_UART_RX_STATE_NONE, (unsigned char)data[i]);
  }

  generic_raw_uart_rx_completed(port->raw_uart);

  usb_submit_urb(port->read_urb, GFP_ATOMIC);
}

static void hb_rf_usb_process_write_urb(struct urb *urb)
{
  struct hb_rf_usb_port_s *port = urb->context;
  unsigned long lock_flags;

  spin_lock_irqsave(&port->is_in_tx_lock, lock_flags);
  port->is_in_tx = false;
  spin_unlock_irqrestore(&port->is_in_tx_lock, lock_flags);

  generic_raw_uart_tx_queued(port->raw_uart);
}

static int hb_rf_usb_start_connection(struct generic_raw_uart *raw_uart)
{
  struct hb_rf_usb_port_s *port = raw_uart->driver_data;

  usb_submit_urb(port->read_urb, GFP_KERNEL);

  return 0;
}

static void hb_rf_usb_stop_connection(struct generic_raw_uart *raw_uart)
{
  struct hb_rf_usb_port_s *port = raw_uart->driver_data;

  usb_kill_urb(port->write_urb);
  usb_kill_urb(port->read_urb);
}

static void hb_rf_usb_stop_tx(struct generic_raw_uart *raw_uart)
{
  // nothing to do
}

static bool hb_rf_usb_isready_for_tx(struct generic_raw_uart *raw_uart)
{
  struct hb_rf_usb_port_s *port = raw_uart->driver_data;
  bool ret;
  unsigned long lock_flags;

  spin_lock_irqsave(&port->is_in_tx_lock, lock_flags);
  ret = !port->is_in_tx;
  spin_unlock_irqrestore(&port->is_in_tx_lock, lock_flags);

  return ret;
}

static void hb_rf_usb_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len)
{
  struct hb_rf_usb_port_s *port = raw_uart->driver_data;
  unsigned long lock_flags;

  spin_lock_irqsave(&port->is_in_tx_lock, lock_flags);
  port->is_in_tx = true;
  spin_unlock_irqrestore(&port->is_in_tx_lock, lock_flags);

  memcpy(port->write_urb->transfer_buffer, &chr[index], len);
  port->write_urb->transfer_buffer_length = len;
  usb_submit_urb(port->write_urb, GFP_ATOMIC);
}

static void hb_rf_usb_init_tx(struct generic_raw_uart *raw_uart)
{
  // nothing to do
}

static int hb_rf_usb_get_gpio_pin_number(struct generic_raw_uart *raw_uart, enum generic_raw_uart_pin pin)
{
  struct hb_rf_usb_port_s *port = raw_uart->driver_data;

  switch(pin)
  {
    case GENERIC_RAW_UART_PIN_BLUE:
      return port->gc.base;
    case GENERIC_RAW_UART_PIN_GREEN:
      return port->gc.base + 1;
    case GENERIC_RAW_UART_PIN_RED:
      return port->gc.base + 2;
    case GENERIC_RAW_UART_PIN_RESET:
      return port->gc.base + 3;
  }
  return 0;
}

static struct raw_uart_driver hb_rf_usb = {
  .get_gpio_pin_number = hb_rf_usb_get_gpio_pin_number,
  .start_connection = hb_rf_usb_start_connection,
  .stop_connection = hb_rf_usb_stop_connection,
  .init_tx = hb_rf_usb_init_tx,
  .isready_for_tx = hb_rf_usb_isready_for_tx,
  .tx_chars = hb_rf_usb_tx_chars,
  .stop_tx = hb_rf_usb_stop_tx,
  .tx_chunk_size = TX_CHUNK_SIZE,
  .tx_bulktransfer_size = TX_CHUNK_SIZE,
};

static int hb_rf_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
  struct hb_rf_usb_port_s *port = kzalloc(sizeof(struct hb_rf_usb_port_s), GFP_KERNEL);

  usb_set_intfdata(interface, port);

  port->udev = interface_to_usbdev(interface);
  dev_info(&port->udev->dev, "Found HB-RF-USB at usb-%s-%s\n", port->udev->bus->bus_name, port->udev->devpath);

  spin_lock_init(&port->is_in_tx_lock);
  spin_lock_init(&port->gpio_lock);

  port->gc.label = "hb-rf-usb-gpio";
  port->gc.ngpio = 4;
  port->gc.request = hb_rf_usb_gpio_request;
  port->gc.get_direction = hb_rf_usb_gpio_direction_get;
  port->gc.direction_input = hb_rf_usb_gpio_direction_input;
  port->gc.direction_output = hb_rf_usb_gpio_direction_output;
  port->gc.get = hb_rf_usb_gpio_get;
  port->gc.set = hb_rf_usb_gpio_set;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
  port->gc.get_multiple = hb_rf_usb_gpio_get_multiple;
#endif
  port->gc.set_multiple = hb_rf_usb_gpio_set_multiple;
  port->gc.owner = THIS_MODULE;
  port->gc.parent = &port->udev->dev;
  port->gc.base = -1;
  port->gc.can_sleep = false;

  port->gpio_direction = 0x07; // LED pins to output, Reset pin to input (resulting in floating reset pin)
  port->gpio_value = 0x06; // activate Red and green LED at startup
  hb_rf_usb_set_cbus_pins(port);

  gpiochip_add_data(&port->gc, 0);

  port->raw_uart = generic_raw_uart_probe(&port->udev->dev, &hb_rf_usb, port);

  hb_rf_usb_init_uart(port);

  return 0;
}

static void hb_rf_usb_disconnect(struct usb_interface *interface)
{
  struct hb_rf_usb_port_s *port = usb_get_intfdata(interface);

  usb_kill_urb(port->write_urb);
  usb_free_urb(port->write_urb);
  kfree(port->write_buffer);

  usb_kill_urb(port->read_urb);
  usb_free_urb(port->read_urb);
  kfree(port->read_buffer);

  generic_raw_uart_remove(port->raw_uart, &port->udev->dev, &hb_rf_usb);

  gpiochip_remove(&port->gc);

  port->gpio_value = 0;
  hb_rf_usb_set_bitmode(port, FTDI_SIO_BITMODE_RESET);

  kfree(port);
}

static struct usb_driver hb_rf_usb_driver = {
  .name = "hb_rf_usb",
  .id_table = usbid,
  .probe = hb_rf_usb_probe,
  .disconnect = hb_rf_usb_disconnect,
};

static int __init hb_rf_usb_init(void)
{
  if (usb_register(&hb_rf_usb_driver))
  {
    printk("HB-RF-USB: unabled to register USB driver\n");
    return -EIO;
  }

  return 0;
}

static void __exit hb_rf_usb_exit(void)
{
  usb_deregister(&hb_rf_usb_driver);
}

module_init(hb_rf_usb_init);
module_exit(hb_rf_usb_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");
MODULE_DESCRIPTION("HB-RF-USB raw uart driver for communication of piVCCU with the HM-MOD-RPI-PCB and RPI-RF-MOD radio modules");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");

