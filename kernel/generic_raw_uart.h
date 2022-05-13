/*-----------------------------------------------------------------------------
 * Copyright (c) 2022 by Alexander Reinert
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
#include <linux/module.h>

#define BAUD 115200

#define MAX_DEVICE_TYPE_LEN 64

enum generic_raw_uart_rx_flags
{
  GENERIC_RAW_UART_RX_STATE_NONE = 0,
  GENERIC_RAW_UART_RX_STATE_BREAK = 1,
  GENERIC_RAW_UART_RX_STATE_PARITY = 2,
  GENERIC_RAW_UART_RX_STATE_FRAME = 4,
  GENERIC_RAW_UART_RX_STATE_OVERRUN = 8,
};

enum generic_raw_uart_pin
{
  GENERIC_RAW_UART_PIN_BLUE = 0,
  GENERIC_RAW_UART_PIN_GREEN = 1,
  GENERIC_RAW_UART_PIN_RED = 2,
  GENERIC_RAW_UART_PIN_RESET = 3,
  GENERIC_RAW_UART_PIN_ALT_RESET = 4,
};

struct generic_raw_uart
{
  void *private;
  void *driver_data;
  int dev_number;
};

struct raw_uart_driver
{
  struct module *owner;

  int (*start_connection)(struct generic_raw_uart *raw_uart);
  void (*stop_connection)(struct generic_raw_uart *raw_uart);

  void (*init_tx)(struct generic_raw_uart *raw_uart);
  bool (*isready_for_tx)(struct generic_raw_uart *raw_uart);
  void (*tx_chars)(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len);
  void (*stop_tx)(struct generic_raw_uart *raw_uart);

  int (*get_gpio_pin_number)(struct generic_raw_uart *raw_uart, enum generic_raw_uart_pin);
  int (*reset_radio_module)(struct generic_raw_uart *raw_uart);

  int tx_chunk_size;
  int tx_bulktransfer_size;

  int (*get_device_type)(struct generic_raw_uart *raw_uart, char *page);
};

struct hb_usb_device_info
{
  uint32_t vendorhash;
  uint32_t pkey[16];
  bool enforce_verification;
};

#define HB_DEV_KEY(...) __VA_ARGS__
#define HB_USB_DEVICE(_vend, _prod, _vendorhash, _enforce_verification, _pkey) \
  USB_DEVICE((_vend), (_prod)), \
  .driver_info = (kernel_ulong_t)&((struct hb_usb_device_info) { .vendorhash = (_vendorhash), .enforce_verification = _enforce_verification, .pkey = { HB_DEV_KEY _pkey } } )

extern struct generic_raw_uart *generic_raw_uart_probe(struct device *dev, struct raw_uart_driver *driver, void *driver_data);
extern int generic_raw_uart_set_connection_state(struct generic_raw_uart *raw_uart, bool state);
extern int generic_raw_uart_remove(struct generic_raw_uart *raw_uart);
extern void generic_raw_uart_tx_queued(struct generic_raw_uart *raw_uart);
extern void generic_raw_uart_handle_rx_char(struct generic_raw_uart *raw_uart, enum generic_raw_uart_rx_flags, unsigned char);
extern void generic_raw_uart_rx_completed(struct generic_raw_uart *raw_uart);

extern bool generic_raw_uart_verify_dkey(struct device *dev, unsigned char *dkey, int dkey_len, unsigned char *skey, uint32_t *pkey, int bytes);

#define module_raw_uart_driver(__module_name, __raw_uart_driver, __of_match)              \
  static struct generic_raw_uart *__raw_uart_driver##_raw_uart;                           \
  static int __##__raw_uart_driver##_probe(struct platform_device *pdev)                  \
  {                                                                                       \
    struct device *dev = &pdev->dev;                                                      \
    int err = __raw_uart_driver##_probe(pdev);                                            \
                                                                                          \
    if (err)                                                                              \
    {                                                                                     \
      dev_err(dev, "failed to initialize generic_raw_uart module");                       \
      return err;                                                                         \
    }                                                                                     \
                                                                                          \
    __raw_uart_driver##_raw_uart = generic_raw_uart_probe(dev, &__raw_uart_driver, NULL); \
    if (IS_ERR_OR_NULL(__raw_uart_driver##_raw_uart))                                     \
    {                                                                                     \
      dev_err(dev, "failed to initialize generic_raw_uart module");                       \
      return PTR_ERR(__raw_uart_driver##_raw_uart);                                       \
    }                                                                                     \
                                                                                          \
    return 0;                                                                             \
  }                                                                                       \
                                                                                          \
  static int __##__raw_uart_driver##_remove(struct platform_device *pdev)                 \
  {                                                                                       \
    int err;                                                                              \
    struct device *dev = &pdev->dev;                                                      \
                                                                                          \
    err = generic_raw_uart_remove(__raw_uart_driver##_raw_uart); \
    if (err)                                                                              \
    {                                                                                     \
      dev_err(dev, "failed to remove generic_raw_uart module");                           \
      return err;                                                                         \
    }                                                                                     \
                                                                                          \
    return __raw_uart_driver##_remove(pdev);                                              \
  }                                                                                       \
                                                                                          \
  static struct platform_driver __raw_uart_driver_platform_driver = {                     \
      .probe = __##__raw_uart_driver##_probe,                                             \
      .remove = __##__raw_uart_driver##_remove,                                           \
      .driver = {                                                                         \
          .owner = THIS_MODULE,                                                           \
          .name = __module_name,                                                          \
          .of_match_table = __of_match,                                                   \
      },                                                                                  \
  };                                                                                      \
                                                                                          \
  module_platform_driver(__raw_uart_driver_platform_driver);                              \
  MODULE_DEVICE_TABLE(of, __of_match);
