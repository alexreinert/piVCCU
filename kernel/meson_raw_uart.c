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
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/reset.h>

#include "generic_raw_uart.h"

#define MODULE_NAME "meson_raw_uart"
#define TX_CHUNK_SIZE 11

#define MESON_WFIFO    0x00
#define MESON_RFIFO    0x04
#define MESON_CONTROL  0x08
#define MESON_STATUS   0x0c
#define MESON_MISC     0x10
#define MESON_REG5     0x14

#define MESON_TX_FULL  BIT(21)
#define MESON_RX_EMPTY BIT(20)

#define MESON_RX_EN    BIT(13)
#define MESON_TX_EN    BIT(12)

#define MESON_TX_INT_EN   BIT(28)
#define MESON_RX_INT_EN   BIT(27)

#define MESON_TX_INT_TRESH 8 << 8 /* interrupt on less than 8 chars in tx fifo */
#define MESON_RX_INT_TRESH 1      /* interrupt on each char */

#define MESON_CLEAR_ERROR BIT(24)
#define MESON_PARITY_ERR  BIT(16)
#define MESON_FRAME_ERR   BIT(17)
#define MESON_OVERRUN_ERR BIT(24)

#define MESON_BUSY_MASK   BIT(25) | BIT(26)

#define MESON_RX_RST      BIT(23)
#define MESON_TX_RST      BIT(22)

static int meson_raw_uart_start_connection(struct generic_raw_uart *raw_uart);
static void meson_raw_uart_stop_connection(struct generic_raw_uart *raw_uart);
static void meson_raw_uart_stop_tx(struct generic_raw_uart *raw_uart);
static bool meson_raw_uart_isready_for_tx(struct generic_raw_uart *raw_uart);
static void meson_raw_uart_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len);
static void meson_raw_uart_init_tx(struct generic_raw_uart *raw_uart);
static void meson_raw_uart_rx_chars(struct generic_raw_uart *raw_uart);
static irqreturn_t meson_raw_uart_irq_handle(int irq, void *context);
static int meson_raw_uart_probe(struct generic_raw_uart *raw_uart, struct platform_device *pdev);
static int meson_raw_uart_remove(struct platform_device *pdev);

struct meson_port_s
{
  struct clk *xtal_clk;
  struct clk *pclk_clk;
  struct clk *baud_clk;
  struct device *dev;
  struct reset_control *rst;
  unsigned long mapbase;
  void __iomem *membase;
  unsigned long irq;
};

static struct meson_port_s *meson_port;

static int meson_raw_uart_start_connection(struct generic_raw_uart *raw_uart)
{
  int ret = 0;
  unsigned long val;
  unsigned long clkrate;

  // clear error
  val = readl(meson_port->membase + MESON_CONTROL);
  val |= MESON_CLEAR_ERROR | MESON_RX_RST | MESON_TX_RST;
  writel(val, meson_port->membase + MESON_CONTROL);
  val &= ~(MESON_CLEAR_ERROR | MESON_RX_RST | MESON_TX_RST);
  writel(val, meson_port->membase + MESON_CONTROL);

  val |= MESON_RX_EN | MESON_TX_EN;
  writel(val, meson_port->membase + MESON_CONTROL);

  // 8N1, two wire
  val &= ~(0x3f << 16);
  val |= BIT(15);
  writel(val, meson_port->membase + MESON_CONTROL);

  val |= MESON_RX_INT_EN;
  writel(val, meson_port->membase + MESON_CONTROL);

  // set RX and TX interrupt tresholds
  writel(MESON_RX_INT_TRESH | MESON_TX_INT_TRESH, meson_port->membase + MESON_MISC);

  // Baudrate
  clkrate = clk_get_rate(meson_port->baud_clk);
  if (clkrate == 24000000)
  {
    val = ((clkrate / 3) / BAUD) - 1;
    val |= BIT(24);
  }
  else
  {
    val = ((clkrate * 10 / (BAUD * 4) + 5) / 10) - 1;
  }
  val |= BIT(23);
  writel(val, meson_port->membase + MESON_REG5);

  /*Register interrupt handler*/
  ret = request_irq(meson_port->irq, meson_raw_uart_irq_handle, 0, dev_name(meson_port->dev), (void*)raw_uart);
  if (ret)
  {
    dev_err(meson_port->dev, "irq could not be registered");
    return ret;
  }

  return 0;
}

static void meson_raw_uart_stop_connection(struct generic_raw_uart *raw_uart)
{
  writel(0, meson_port->membase + MESON_CONTROL);  /*Disable UART*/
  writel(0, meson_port->membase + MESON_MISC);  /*Disable interrupts*/

  free_irq(meson_port->irq, raw_uart);
}

static void meson_raw_uart_stop_tx(struct generic_raw_uart *raw_uart)
{
  unsigned long control;

  control = readl(meson_port->membase + MESON_CONTROL);
  control &= ~MESON_TX_INT_EN;

  writel(control, meson_port->membase + MESON_CONTROL);
}

static bool meson_raw_uart_isready_for_tx(struct generic_raw_uart *raw_uart)
{
  return !(readl(meson_port->membase + MESON_STATUS) & MESON_TX_FULL);
}

static void meson_raw_uart_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len)
{
  writel(chr[index], meson_port->membase + MESON_WFIFO);
}

static void meson_raw_uart_init_tx(struct generic_raw_uart *raw_uart)
{
  unsigned long control;

  control = readl(meson_port->membase + MESON_CONTROL);
  control |= MESON_TX_INT_EN;

  writel(control, meson_port->membase + MESON_CONTROL);
}

static void meson_raw_uart_rx_chars(struct generic_raw_uart *raw_uart)
{
  unsigned long status;
  unsigned long control;
  unsigned long data;
  enum generic_raw_uart_rx_flags flags = GENERIC_RAW_UART_RX_STATE_NONE;

  while(1)
  {
    status = readl( meson_port->membase + MESON_STATUS);

    if(status & MESON_RX_EMPTY)
    {
      break;
    }

    data = readl(meson_port->membase + MESON_RFIFO);

    /* Error handling */
    if(status & MESON_PARITY_ERR)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_PARITY;
    }
    if(status & MESON_FRAME_ERR)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_FRAME;
    }
    if(status & MESON_OVERRUN_ERR)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_OVERRUN;
    }

    if (flags != GENERIC_RAW_UART_RX_STATE_NONE) {
      control = readl(meson_port->membase + MESON_CONTROL);

      control |= MESON_CLEAR_ERROR;
      writel(control, meson_port->membase + MESON_CONTROL);

      control &= ~MESON_CLEAR_ERROR;
      writel(control, meson_port->membase + MESON_CONTROL);
    }

    generic_raw_uart_handle_rx_char(raw_uart, flags, (unsigned char)data);
  }

  generic_raw_uart_rx_completed(raw_uart);
}

static irqreturn_t meson_raw_uart_irq_handle(int irq, void *context)
{
  struct generic_raw_uart *raw_uart = context;

  if(!(readl(meson_port->membase + MESON_STATUS) & MESON_RX_EMPTY))
  {
    meson_raw_uart_rx_chars(raw_uart);
  }

  if(readl(meson_port->membase + MESON_CONTROL) & MESON_TX_INT_EN)
  {
    if(!(readl(meson_port->membase + MESON_STATUS) & MESON_TX_FULL))
    {
      generic_raw_uart_tx_queued(raw_uart);
    }
  }

  return IRQ_HANDLED;
}

static struct raw_uart_driver meson_raw_uart = {
  .start_connection = meson_raw_uart_start_connection,
  .stop_connection = meson_raw_uart_stop_connection,
  .init_tx = meson_raw_uart_init_tx,
  .isready_for_tx = meson_raw_uart_isready_for_tx,
  .tx_chars = meson_raw_uart_tx_chars,
  .stop_tx = meson_raw_uart_stop_tx,
  .tx_chunk_size = TX_CHUNK_SIZE,
  .tx_bulktransfer_size = 1,
};

static inline struct clk *meson_raw_uart_probe_clk(struct device *dev, const char *id)
{
  struct clk *clk = NULL;
  int ret;

  clk = devm_clk_get(dev, id);
  if (IS_ERR(clk))
    return clk;

  ret = clk_prepare_enable(clk);
  if (ret)
    return ERR_PTR(ret);

  ret = devm_add_action(dev, (void(*)(void *))clk_disable_unprepare, clk);
  if (ret)
    clk_disable_unprepare(clk);

  return clk;
}

static int meson_raw_uart_probe(struct generic_raw_uart *raw_uart, struct platform_device *pdev)
{
  int err;
  struct device *dev = &pdev->dev;
  struct resource *ioresource;

  /* alloc private resources */
  meson_port = kzalloc(sizeof(struct meson_port_s), GFP_KERNEL);
  if (!meson_port) {
    err = -ENOMEM;
    goto failed_inst_alloc;
  }

  /* Get mapbase and membase */
  ioresource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (ioresource) {
    meson_port->mapbase = ioresource->start;
    meson_port->membase = ioremap(ioresource->start, resource_size(ioresource));
  } else {
    dev_err(dev, "failed to get IO resource\n");
    err = -ENOENT;
    goto failed_get_resource;
  }

  /* get irq */
  meson_port->irq = platform_get_irq(pdev, 0);
  if (meson_port->irq <= 0) {
    dev_err(dev, "failed to get irq\n");
    err = -ENOENT;
    goto failed_get_resource;
  }

  /* get clocks */
  meson_port->pclk_clk = meson_raw_uart_probe_clk(dev, "pclk");
  if (IS_ERR(meson_port->pclk_clk))
  {
    dev_err(dev, "failed to get pclk clock\n");
    err = PTR_ERR(meson_port->pclk_clk);
    goto failed_get_clock;
  }

  meson_port->xtal_clk = meson_raw_uart_probe_clk(dev, "xtal");
  if (IS_ERR(meson_port->xtal_clk))
  {
    dev_err(dev, "failed to get xtal clock\n");
    err = PTR_ERR(meson_port->xtal_clk);
    goto failed_get_clock;
  }

  meson_port->baud_clk = meson_raw_uart_probe_clk(dev, "baud");
  if (IS_ERR(meson_port->baud_clk))
  {
    dev_err(dev, "failed to get baud clock\n");
    err = PTR_ERR(meson_port->baud_clk);
    goto failed_get_clock;
  }

  /* get reset device */
  meson_port->rst = devm_reset_control_get(dev, NULL);
  if (!IS_ERR(meson_port->rst)) {
    reset_control_deassert(meson_port->rst);
  }

  meson_port->dev = dev;

  dev_info(dev, "Initialized meson device; mapbase=0x%08lx; irq=%lu; clockrate=%lu", meson_port->mapbase, meson_port->irq, clk_get_rate(meson_port->baud_clk));

  return 0;

failed_get_clock:
failed_get_resource:
  kfree(meson_port);
failed_inst_alloc:
  return err;
}

static int meson_raw_uart_remove(struct platform_device *pdev)
{
  reset_control_assert(meson_port->rst);

  kfree(meson_port);
  return 0;
}

static struct of_device_id meson_raw_uart_of_match[] = {
  { .compatible = "pivccu,meson" },
  { /* sentinel */ },
};

module_raw_uart_driver(MODULE_NAME, meson_raw_uart, meson_raw_uart_of_match);

MODULE_ALIAS("platform:meson-raw-uart");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.2");
MODULE_DESCRIPTION("MESON raw uart driver for communication of piVCCU with the HM-MOD-RPI-PCB and RPI-RF-MOD radio modules");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");

