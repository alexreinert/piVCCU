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
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/gpio/driver.h>
#include <linux/version.h>
#include <linux/crc32.h>
#include "generic_raw_uart.h"

#include "stack_protector.include"

#define TX_CHUNK_SIZE 11

#define BUFFER_SIZE 256

#define REQTYPE_HOST_TO_INTERFACE 0x41
#define REQTYPE_INTERFACE_TO_HOST 0xc1
#define REQTYPE_HOST_TO_DEVICE 0x40
#define REQTYPE_DEVICE_TO_HOST 0xc0

#define CP2102N_IFC_ENABLE 0x00
#define CP2102N_SET_LINE_CTL 0x03
#define CP2102N_PURGE 0x12
#define CP2102N_EMBED_EVENTS 0x15
#define CP2102N_SET_USB_RECV_TIMEOUT 0x17
#define CP2102N_SET_BAUDRATE 0x1e
#define CP2102N_VENDOR_SPECIFIC 0xff

#define UART_ENABLE 0x0001
#define UART_DISABLE 0x0000

#define PURGE_ALL 0x000f

#define BITS_DATA_8 0x0800
#define BITS_PARITY_NONE 0x0000
#define BITS_STOP_1 0x0000

#define CP2102N_WRITE_LATCH 0x37E1
#define CP2102N_READ_CONFIG 0x000e
#define CP2102N_GET_CHIPTYPE 0x370B
#define CP2102N_GET_FW_VER 0x0010

#define CP2102N_CONFIG_SIZE 0x02a6

#define LED_GPIO_MASK BIT(1) | BIT(2) | BIT(3)
#define RESET_GPIO_MASK BIT(0)

#define EMBED_EVENT_CHAR 0xff

struct hb_rf_usb_2_port_s
{
  struct generic_raw_uart *raw_uart;

  bool invert_reset;

  unsigned char part_num;

  struct urb *write_urb;
  unsigned char *write_buffer;
  bool is_in_tx;
  spinlock_t is_in_tx_lock;

  struct urb *read_urb;
  unsigned char *read_buffer;

  struct usb_device *udev;
  struct usb_interface *iface;
  struct gpio_chip gc;

  spinlock_t gpio_lock;
  u8 gpio_value;

  struct kref kref;
};

static struct usb_device_id usbid[] = {
  { HB_USB_DEVICE(0x10c4, 0x8c07, 0x60d01cf9ul, false, (0xd7cc3f44ul, 0xf50048a8ul, 0x3cf61d60ul, 0xae8460d1ul, 0x272a5876ul, 0x2dc161f5ul, 0x7737fbaeul, 0x88c996b2ul, 0xed521e88ul, 0xdd24b0acul, 0x4b7c49a4ul, 0xcc457acbul, 0xb2079847ul, 0x6745032aul, 0xaf25d41ful, 0xbecfb9f3ul)) },
  { HB_USB_DEVICE(0x1b1f, 0xc020, 0, false, (0)) },
  { HB_USB_DEVICE(0x10c4, 0x8d81, 0, true, (0x08b1480eul, 0x8f349c82ul, 0xe56ba2d8ul, 0x84fe66b7ul, 0x0f102855ul, 0x79bd41d5ul, 0x8d8905e0ul, 0xe9c16cc3ul, 0xbb188368ul, 0x4167f9bful, 0x05084458ul, 0x24a171daul, 0xfdbf5b3bul, 0xec31efeaul, 0x1653f3eful, 0x495f8104ul)) },
  { HB_USB_DEVICE(0x10c4, 0x8d91, 0, true, (0x263fb637ul, 0x01f31c2cul, 0xf413b1f1ul, 0x9e1bfd39ul, 0x4d3bdd3bul, 0xc6f9859cul, 0xfd9a1fd8ul, 0xfe4dea4dul, 0x7d2777d8ul, 0x53be386dul, 0xfbf7255eul, 0x1c8a39feul, 0xbdf5be92ul, 0x535f9fc1ul, 0x56bb3a13ul, 0x752d919eul)) },
  { }
};

MODULE_DEVICE_TABLE(usb, usbid);

static void hb_rf_usb_2_set_gpio_on_device_completion(struct urb *urb)
{
  kfree(urb->setup_packet);
  usb_free_urb(urb);
}

static void hb_rf_usb_2_set_gpio_on_device(struct hb_rf_usb_2_port_s *port, u8 mask, u8 value)
{
  struct usb_ctrlrequest *dr;
  struct urb *urb;

  dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);

  dr->bRequestType = REQTYPE_HOST_TO_DEVICE;
  dr->bRequest = CP2102N_VENDOR_SPECIFIC;
  dr->wValue = cpu_to_le16(CP2102N_WRITE_LATCH);
  dr->wIndex = cpu_to_le16(value << 8 | mask);
  dr->wLength = 0;

  urb = usb_alloc_urb(0, GFP_ATOMIC);

  usb_fill_control_urb(urb, port->udev, usb_sndctrlpipe(port->udev, 0), (void *)dr, NULL, 0, hb_rf_usb_2_set_gpio_on_device_completion, NULL);

  usb_submit_urb(urb, GFP_ATOMIC);
}

static int hb_rf_usb_2_gpio_request(struct gpio_chip *gc, unsigned int offset)
{
  if (offset > 2)
    return -ENODEV;

  if (gc->owner && !try_module_get(gc->owner))
    return -ENODEV;

  return 0;
}

static void hb_rf_usb_2_gpio_free(struct gpio_chip *gc, unsigned offset)
{
  module_put(gc->owner);
}

static int hb_rf_usb_2_gpio_direction_get(struct gpio_chip *gc, unsigned int gpio)
{
  return 0;
}

static int hb_rf_usb_2_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
  return -EPERM;
}

static int hb_rf_usb_2_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio, int value)
{
  return 0;
}

static int hb_rf_usb_2_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
  struct hb_rf_usb_2_port_s *port = container_of(gc, struct hb_rf_usb_2_port_s, gc);

  return port->gpio_value & BIT(gpio);
}

static void hb_rf_usb_2_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
  struct hb_rf_usb_2_port_s *port = container_of(gc, struct hb_rf_usb_2_port_s, gc);
  unsigned long lock_flags;

  spin_lock_irqsave(&port->gpio_lock, lock_flags);

  if (value)
    port->gpio_value |= BIT(gpio);
  else
    port->gpio_value &= ~BIT(gpio);

  hb_rf_usb_2_set_gpio_on_device(port, LED_GPIO_MASK, port->gpio_value << 1);

  spin_unlock_irqrestore(&port->gpio_lock, lock_flags);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
static int hb_rf_usb_2_gpio_get_multiple(struct gpio_chip *gc, unsigned long *mask, unsigned long *bits)
{
  struct hb_rf_usb_2_port_s *port = container_of(gc, struct hb_rf_usb_2_port_s, gc);

  *bits = port->gpio_value & *mask;

  return 0;
}
#endif

static void hb_rf_usb_2_gpio_set_multiple(struct gpio_chip *gc, unsigned long *mask, unsigned long *bits)
{
  struct hb_rf_usb_2_port_s *port = container_of(gc, struct hb_rf_usb_2_port_s, gc);
  unsigned long lock_flags;

  spin_lock_irqsave(&port->gpio_lock, lock_flags);

  port->gpio_value &= ~(*mask);
  port->gpio_value |= *bits & *mask;

  hb_rf_usb_2_set_gpio_on_device(port, LED_GPIO_MASK, port->gpio_value << 1);

  spin_unlock_irqrestore(&port->gpio_lock, lock_flags);
}

static int hb_rf_usb_2_start_connection(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_2_stop_connection(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_2_stop_tx(struct generic_raw_uart *raw_uart);
static bool hb_rf_usb_2_isready_for_tx(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_2_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len);
static void hb_rf_usb_2_init_tx(struct generic_raw_uart *raw_uart);
static void hb_rf_usb_2_process_read_urb(struct urb *urb);
static void hb_rf_usb_2_process_write_urb(struct urb *urb);
static void hb_rf_usb_2_delete(struct kref *kref);

static void hb_rf_usb_2_init_uart(struct hb_rf_usb_2_port_s *port)
{
  struct usb_host_interface *iface_desc;
  struct usb_endpoint_descriptor *epd;
  int i;
  u32 baudrate;
  void *dmabuf;

  // set 8N1
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), CP2102N_SET_LINE_CTL, REQTYPE_HOST_TO_INTERFACE, BITS_DATA_8 | BITS_PARITY_NONE | BITS_STOP_1, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);

  // set baudrate
  baudrate = 115200;
  dmabuf = kmemdup(&baudrate, sizeof(baudrate), GFP_KERNEL);
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), CP2102N_SET_BAUDRATE, REQTYPE_HOST_TO_INTERFACE, 0, 0, dmabuf, sizeof(baudrate), USB_CTRL_SET_TIMEOUT);
  kfree(dmabuf);

  iface_desc = port->iface->cur_altsetting;
  for (i = 0; i < iface_desc->desc.bNumEndpoints; i++)
  {
    epd = &iface_desc->endpoint[i].desc;

    if (usb_endpoint_is_bulk_in(epd))
    {
      port->read_urb = usb_alloc_urb(0, GFP_KERNEL);
      port->read_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
      usb_fill_bulk_urb(port->read_urb, port->udev, usb_rcvbulkpipe(port->udev, epd->bEndpointAddress), port->read_buffer, BUFFER_SIZE, hb_rf_usb_2_process_read_urb, port);
    }
    else if (usb_endpoint_is_bulk_out(epd))
    {
      port->write_urb = usb_alloc_urb(0, GFP_KERNEL);
      port->write_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
      usb_fill_bulk_urb(port->write_urb, port->udev, usb_sndbulkpipe(port->udev, epd->bEndpointAddress), port->write_buffer, BUFFER_SIZE, hb_rf_usb_2_process_write_urb, port);
    }
  }
}

static void hb_rf_usb_2_process_read_urb(struct urb *urb)
{
  struct hb_rf_usb_2_port_s *port = urb->context;

  enum generic_raw_uart_rx_flags flags;
  unsigned char *data = (unsigned char *)urb->transfer_buffer;
  unsigned char status;
  int i;

  for (i = 0; i < urb->actual_length; i++)
  {
    if (data[i] == EMBED_EVENT_CHAR)
    {
      i++;
      switch (data[i])
      {
      case 0:
        generic_raw_uart_handle_rx_char(port->raw_uart, GENERIC_RAW_UART_RX_STATE_NONE, EMBED_EVENT_CHAR);
        break;
      case 1:
      case 2:
        i++;
        status = data[i];
        flags = GENERIC_RAW_UART_RX_STATE_NONE;

        if (status & BIT(1))
        {
          flags |= GENERIC_RAW_UART_RX_STATE_OVERRUN;
        }
        if (status & BIT(2))
        {
          flags |= GENERIC_RAW_UART_RX_STATE_PARITY;
        }
        if (status & BIT(3))
        {
          flags |= GENERIC_RAW_UART_RX_STATE_FRAME;
        }
        if (status & BIT(4))
        {
          flags |= GENERIC_RAW_UART_RX_STATE_BREAK;
        }

        if (status & BIT(0))
        {
          i++;
          generic_raw_uart_handle_rx_char(port->raw_uart, flags, data[i]);
        }
        else
        {
          generic_raw_uart_handle_rx_char(port->raw_uart, flags, 0);
        }
        break;
      }
    }
    else
    {
      generic_raw_uart_handle_rx_char(port->raw_uart, GENERIC_RAW_UART_RX_STATE_NONE, (unsigned char)data[i]);
    }
  }

  generic_raw_uart_rx_completed(port->raw_uart);

  usb_submit_urb(port->read_urb, GFP_ATOMIC);
}

static void hb_rf_usb_2_process_write_urb(struct urb *urb)
{
  struct hb_rf_usb_2_port_s *port = urb->context;
  unsigned long lock_flags;

  spin_lock_irqsave(&port->is_in_tx_lock, lock_flags);
  port->is_in_tx = false;
  spin_unlock_irqrestore(&port->is_in_tx_lock, lock_flags);

  generic_raw_uart_tx_queued(port->raw_uart);
}

static int hb_rf_usb_2_start_connection(struct generic_raw_uart *raw_uart)
{
  struct hb_rf_usb_2_port_s *port = raw_uart->driver_data;

  kref_get(&port->kref);

  // enable uart
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), CP2102N_IFC_ENABLE, REQTYPE_HOST_TO_INTERFACE, UART_ENABLE, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
  // set embedded event char
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), CP2102N_EMBED_EVENTS, REQTYPE_HOST_TO_INTERFACE, EMBED_EVENT_CHAR, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);

  usb_submit_urb(port->read_urb, GFP_KERNEL);

  return 0;
}

static void hb_rf_usb_2_stop_connection(struct generic_raw_uart *raw_uart)
{
  struct hb_rf_usb_2_port_s *port = raw_uart->driver_data;

  usb_kill_urb(port->write_urb);
  usb_kill_urb(port->read_urb);

  // clear fifo
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), CP2102N_PURGE, REQTYPE_HOST_TO_INTERFACE, PURGE_ALL, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);
  // disable uart
  usb_control_msg(port->udev, usb_sndctrlpipe(port->udev, 0), CP2102N_IFC_ENABLE, REQTYPE_HOST_TO_INTERFACE, UART_DISABLE, 0, NULL, 0, USB_CTRL_SET_TIMEOUT);

  kref_put(&port->kref, hb_rf_usb_2_delete);
}

static void hb_rf_usb_2_stop_tx(struct generic_raw_uart *raw_uart)
{
  // nothing to do
}

static bool hb_rf_usb_2_isready_for_tx(struct generic_raw_uart *raw_uart)
{
  struct hb_rf_usb_2_port_s *port = raw_uart->driver_data;
  bool ret;
  unsigned long lock_flags;

  spin_lock_irqsave(&port->is_in_tx_lock, lock_flags);
  ret = !port->is_in_tx;
  spin_unlock_irqrestore(&port->is_in_tx_lock, lock_flags);

  return ret;
}

static void hb_rf_usb_2_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len)
{
  struct hb_rf_usb_2_port_s *port = raw_uart->driver_data;
  unsigned long lock_flags;

  spin_lock_irqsave(&port->is_in_tx_lock, lock_flags);
  port->is_in_tx = true;
  spin_unlock_irqrestore(&port->is_in_tx_lock, lock_flags);

  memcpy(port->write_urb->transfer_buffer, &chr[index], len);
  port->write_urb->transfer_buffer_length = len;
  usb_submit_urb(port->write_urb, GFP_ATOMIC);
}

static void hb_rf_usb_2_init_tx(struct generic_raw_uart *raw_uart)
{
  // nothing to do
}

static int hb_rf_usb_2_get_gpio_pin_number(struct generic_raw_uart *raw_uart, enum generic_raw_uart_pin pin)
{
  struct hb_rf_usb_2_port_s *port = raw_uart->driver_data;

  if (port->part_num >= 0x20 && port->part_num <= 0x22)
  {
    switch (pin)
    {
    case GENERIC_RAW_UART_PIN_BLUE:
      return port->gc.base + 2;
    case GENERIC_RAW_UART_PIN_GREEN:
      return port->gc.base + 1;
    case GENERIC_RAW_UART_PIN_RED:
      return port->gc.base;
    default:
      return 0;
    }
  }
  else
  {
    return 0;
  }
}

static int hb_rf_usb_2_reset_radio_module(struct generic_raw_uart *raw_uart)
{
  struct hb_rf_usb_2_port_s *port = raw_uart->driver_data;

  if (port->part_num >= 0x20 && port->part_num <= 0x22)
  {
    hb_rf_usb_2_set_gpio_on_device(port, RESET_GPIO_MASK, (port->invert_reset ? 0 : 1));
    msleep(50);
    hb_rf_usb_2_set_gpio_on_device(port, RESET_GPIO_MASK, (port->invert_reset ? 1 : 0));
    msleep(50);
    return 0;
  }
  return -ENOSYS;
}

static int hb_rf_usb_2_get_device_type(struct generic_raw_uart *raw_uart, char *page)
{
  struct hb_rf_usb_2_port_s *port = raw_uart->driver_data;
  return snprintf(page, MAX_DEVICE_TYPE_LEN, "%s@usb-%s-%s", port->udev->product, port->udev->bus->bus_name, port->udev->devpath);
}

static struct raw_uart_driver hb_rf_usb_2 = {
    .owner = THIS_MODULE,
    .get_gpio_pin_number = hb_rf_usb_2_get_gpio_pin_number,
    .reset_radio_module = hb_rf_usb_2_reset_radio_module,
    .start_connection = hb_rf_usb_2_start_connection,
    .stop_connection = hb_rf_usb_2_stop_connection,
    .init_tx = hb_rf_usb_2_init_tx,
    .isready_for_tx = hb_rf_usb_2_isready_for_tx,
    .tx_chars = hb_rf_usb_2_tx_chars,
    .stop_tx = hb_rf_usb_2_stop_tx,
    .get_device_type = hb_rf_usb_2_get_device_type,
    .tx_chunk_size = TX_CHUNK_SIZE,
    .tx_bulktransfer_size = TX_CHUNK_SIZE,
};

static int hb_rf_usb_2_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
  struct hb_rf_usb_2_port_s *port;
  struct usb_device *udev = usb_get_dev(interface_to_usbdev(interface));
  const struct usb_device_id *match = usb_match_id(interface, usbid);
  unsigned char *buffer;
  bool invert_reset = false;
  unsigned char part_num;

  if (!match)
  {
    dev_err(&udev->dev, "Unsupported USB device\n");
    return ENODEV;
  }

  if (match->driver_info != 0)
  {
    struct hb_usb_device_info *hbudi = (struct hb_usb_device_info*)match->driver_info;

    if ((hbudi->vendorhash != 0) && (hbudi->vendorhash != (crc32(0xffffffff, udev->manufacturer, strlen(udev->manufacturer)) ^ 0xffffffff)))
    {
      dev_err(&udev->dev, "Unsupported manufacturer %s\n", udev->manufacturer);
      kfree(buffer);
      return -ENODEV;
    }
  }

  buffer = kmalloc(CP2102N_CONFIG_SIZE, GFP_KERNEL);
  if (!buffer)
    return -ENOMEM;

  usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), CP2102N_VENDOR_SPECIFIC, REQTYPE_DEVICE_TO_HOST, CP2102N_GET_CHIPTYPE, 0, buffer, 1, USB_CTRL_GET_TIMEOUT);
  part_num = buffer[0];

  if (part_num >= 0x20 && part_num <= 0x22)
  {
    usb_control_msg(udev, usb_rcvctrlpipe(udev, 0), CP2102N_VENDOR_SPECIFIC, REQTYPE_DEVICE_TO_HOST, CP2102N_READ_CONFIG, 0, buffer, CP2102N_CONFIG_SIZE, USB_CTRL_GET_TIMEOUT);

    switch (part_num)
    {
      case 0x20:
      case 0x21:
        invert_reset = ((buffer[587] & 8) != 0);
        break;

      case 0x22:
        invert_reset = ((buffer[586] & 1) != 0);
        break;
    }

    if (match->driver_info != 0)
    {
      struct hb_usb_device_info *hbudi = (struct hb_usb_device_info*)match->driver_info;

      if (hbudi->pkey[0] != 0)
      {
        memcpy(buffer + buffer[191] + 245, buffer + 331 + strlen(udev->product) * 2, 20);
        memcpy(buffer + 231 + buffer[191], buffer + 62 + buffer[60], buffer[45]);
        memcpy(buffer + 245 + strlen(udev->product) * 2, buffer + 452 + buffer[450], buffer[192] * 10);
        memcpy(buffer + buffer[191] + 257, buffer + 518, buffer[60]);

        if ((buffer[449] == 0xff) && generic_raw_uart_verify_dkey(&udev->dev, udev->serial, strlen(udev->serial), buffer + buffer[192] + 192 + strlen(udev->product) * 2, hbudi->pkey, 32))
        {
          dev_info(&udev->dev, "Successfully verified device signature\n");
        }
        else
        {
          dev_err(&udev->dev, "Could not verify device signature\n");

          if (part_num != 0x22 || hbudi->enforce_verification)
          {
            kfree(buffer);
            return -ENODEV;
          }
        }
      }
    }
  }
  else if (part_num != 0x02)
  {
    dev_err(&udev->dev, "Unsupported chip type %d\n", part_num);
    kfree(buffer);
    return -ENODEV;
  }

  dev_info(&udev->dev, "Found %s with serial %s at usb-%s-%s\n", udev->product, udev->serial, udev->bus->bus_name, udev->devpath);

  if (invert_reset)
    dev_info(&udev->dev, "Using inverted reset logic for radio module\n");

  port = kzalloc(sizeof(struct hb_rf_usb_2_port_s), GFP_KERNEL);
  if (!port)
  {
    kfree(buffer);
    return -ENOMEM;
  }

  usb_set_intfdata(interface, port);

  kref_init(&port->kref);
  port->udev = udev;
  port->iface = usb_get_intf(interface);
  port->invert_reset = invert_reset;
  port->part_num = part_num;

  spin_lock_init(&port->is_in_tx_lock);
  spin_lock_init(&port->gpio_lock);

  if (part_num >= 0x20 && part_num <= 0x22)
  {
    port->gc.label = "hb-rf-usb-2-gpio";
    port->gc.ngpio = 3;
    port->gc.request = hb_rf_usb_2_gpio_request;
    port->gc.free = hb_rf_usb_2_gpio_free;
    port->gc.get_direction = hb_rf_usb_2_gpio_direction_get;
    port->gc.direction_input = hb_rf_usb_2_gpio_direction_input;
    port->gc.direction_output = hb_rf_usb_2_gpio_direction_output;
    port->gc.get = hb_rf_usb_2_gpio_get;
    port->gc.set = hb_rf_usb_2_gpio_set;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
    port->gc.get_multiple = hb_rf_usb_2_gpio_get_multiple;
#endif
    port->gc.set_multiple = hb_rf_usb_2_gpio_set_multiple;
    port->gc.owner = THIS_MODULE;
    port->gc.parent = &port->udev->dev;
    port->gc.base = -1;
    port->gc.can_sleep = false;

    port->gpio_value = 0x03;
    hb_rf_usb_2_set_gpio_on_device(port, LED_GPIO_MASK | RESET_GPIO_MASK, (port->gpio_value << 1) | (invert_reset ? 0 : 1));

    gpiochip_add_data(&port->gc, 0);
  }

  port->raw_uart = generic_raw_uart_probe(&port->udev->dev, &hb_rf_usb_2, port);

  hb_rf_usb_2_init_uart(port);

  kfree(buffer);

  return 0;
}

static void hb_rf_usb_2_disconnect(struct usb_interface *interface)
{
  struct hb_rf_usb_2_port_s *port = usb_get_intfdata(interface);

  usb_kill_urb(port->write_urb);
  usb_kill_urb(port->read_urb);

  if (port->part_num >= 0x20 && port->part_num <= 0x22)
  {
    gpiochip_remove(&port->gc);
  }

  generic_raw_uart_set_connection_state(port->raw_uart, false);

  kref_put(&port->kref, hb_rf_usb_2_delete);
}

static void hb_rf_usb_2_delete(struct kref *kref)
{
  struct hb_rf_usb_2_port_s *port = container_of(kref, struct hb_rf_usb_2_port_s, kref);

  generic_raw_uart_remove(port->raw_uart);

  usb_free_urb(port->write_urb);
  kfree(port->write_buffer);
  usb_free_urb(port->read_urb);
  kfree(port->read_buffer);

  usb_put_intf(port->iface);
  usb_put_dev(port->udev);

  kfree(port);
}

static struct usb_driver hb_rf_usb_2_driver = {
    .name = "hb_rf_usb_2",
    .id_table = usbid,
    .probe = hb_rf_usb_2_probe,
    .disconnect = hb_rf_usb_2_disconnect,
    .no_dynamic_id = 1,
};

static int __init hb_rf_usb_2_init(void)
{
  if (usb_register(&hb_rf_usb_2_driver))
  {
    printk("HB-RF-USB-2: unabled to register USB driver\n");
    return -EIO;
  }

  return 0;
}

static void __exit hb_rf_usb_2_exit(void)
{
  usb_deregister(&hb_rf_usb_2_driver);
}

module_init(hb_rf_usb_2_init);
module_exit(hb_rf_usb_2_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("1.14");
MODULE_DESCRIPTION("HB-RF-USB-2 raw uart driver for communication of debmatic and piVCCU with the HM-MOD-RPI-PCB and RPI-RF-MOD radio modules");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
MODULE_ALIAS("hb_rf_usb-2");

