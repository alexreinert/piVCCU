/*-----------------------------------------------------------------------------
 * Copyright (c) 2017 by Alexander Reinert
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

#define BAUD 115200

enum generic_raw_uart_rx_flags {
  GENERIC_RAW_UART_RX_STATE_NONE = 0,
  GENERIC_RAW_UART_RX_STATE_BREAK = 1,
  GENERIC_RAW_UART_RX_STATE_PARITY = 2,
  GENERIC_RAW_UART_RX_STATE_FRAME = 4,
  GENERIC_RAW_UART_RX_STATE_OVERRUN = 8,
};

struct raw_uart_driver
{
  int (*start_connection)(void);
  void (*stop_connection)(void);

  void (*init_tx)(void);
  bool (*isready_for_tx)(void);
  void (*tx_char)(unsigned char chr);
  void (*stop_tx)(void);

  int tx_chunk_size;
};

extern int generic_raw_uart_probe(struct device *, struct raw_uart_driver *);
extern int generic_raw_uart_remove(struct device *, struct raw_uart_driver *);
extern void generic_raw_uart_tx_queued(void);
extern void generic_raw_uart_handle_rx_char(enum generic_raw_uart_rx_flags, unsigned char);
extern void generic_raw_uart_rx_completed(void);

#define module_raw_uart_driver(__module_name, __raw_uart_driver, __of_match) \
static int __##__raw_uart_driver##_probe(struct platform_device *pdev) \
{ \
  int err; \
  struct device *dev = &pdev->dev; \
 \
  err = generic_raw_uart_probe(dev, &__raw_uart_driver); \
  if (err) \
  { \
    dev_err(dev, "failed to initialize generic_raw_uart module"); \
    return err; \
  } \
 \
  return __raw_uart_driver##_probe(pdev); \
} \
 \
static int __##__raw_uart_driver##_remove(struct platform_device *pdev) \
{ \
  int err; \
  struct device *dev = &pdev->dev; \
 \
  err = generic_raw_uart_remove(dev, &__raw_uart_driver); \
  if (err) \
  { \
    dev_err(dev, "failed to remove generic_raw_uart module"); \
    return err; \
  } \
 \
  return __raw_uart_driver##_remove(pdev); \
} \
 \
static struct platform_driver __raw_uart_driver_platform_driver = { \
 .probe = __##__raw_uart_driver##_probe, \
 .remove = __##__raw_uart_driver##_remove, \
 .driver = { \
    .owner = THIS_MODULE, \
    .name = __module_name, \
    .of_match_table = __of_match, \
  }, \
}; \
 \
module_platform_driver(__raw_uart_driver_platform_driver); \
MODULE_DEVICE_TABLE(of, __of_match);

