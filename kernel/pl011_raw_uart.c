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
#include <linux/amba/serial.h>
#include <linux/version.h>

#include "generic_raw_uart.h"

#include "stack_protector.include"

#define MODULE_NAME "pl011_raw_uart"
#define TX_CHUNK_SIZE 11

static int pl011_raw_uart_start_connection(struct generic_raw_uart *raw_uart);
static void pl011_raw_uart_stop_connection(struct generic_raw_uart *raw_uart);
static void pl011_raw_uart_stop_tx(struct generic_raw_uart *raw_uart);
static bool pl011_raw_uart_isready_for_tx(struct generic_raw_uart *raw_uart);
static void pl011_raw_uart_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len);
static void pl011_raw_uart_init_tx(struct generic_raw_uart *raw_uart);
static void pl011_raw_uart_rx_chars(struct generic_raw_uart *raw_uart);
static irqreturn_t pl011_raw_uart_irq_handle(int irq, void *context);
static int pl011_raw_uart_probe(struct platform_device *pdev);
static int pl011_raw_uart_remove(struct platform_device *pdev);

struct pl011_port_s
{
  struct clk *clk;       /*System clock assigned to the UART device*/
  struct device *dev;    /*System device*/
  unsigned long mapbase; /*physical address of UART registers*/
  void __iomem *membase; /*logical address of UART registers*/
  unsigned long irq;     /*interrupt number*/
};

static struct pl011_port_s *pl011_port;

static int pl011_raw_uart_start_connection(struct generic_raw_uart *raw_uart)
{
  int ret = 0;
  unsigned int bauddiv;
  unsigned long uart_cr;

  /* set baud rate */
  bauddiv = DIV_ROUND_CLOSEST(clk_get_rate(pl011_port->clk) * 4, BAUD);
  writel(bauddiv & 0x3f, pl011_port->membase + UART011_FBRD);
  writel(bauddiv >> 6, pl011_port->membase + UART011_IBRD);

  /* Ensure interrupts from this UART are masked and cleared */
  writel(0, pl011_port->membase + UART011_IMSC);
  writel(0x7ff, pl011_port->membase + UART011_ICR);

  /*Register interrupt handler*/
  ret = request_irq(pl011_port->irq, pl011_raw_uart_irq_handle, IRQF_SHARED, dev_name(pl011_port->dev), raw_uart);
  if (ret)
  {
    dev_err(pl011_port->dev, "irq could not be registered");
    return ret;
  }

  /* enable RX and TX, set RX FIFO threshold to lowest and TX FIFO threshold to mid */
  /*If uart is enabled, wait until it is not busy*/
  while ((readl(pl011_port->membase + UART011_CR) & UART01x_CR_UARTEN) && (readl(pl011_port->membase + UART01x_FR) & UART01x_FR_BUSY))
  {
    schedule();
  }

  uart_cr = readl(pl011_port->membase + UART011_CR);
  uart_cr &= ~(UART011_CR_OUT2 | UART011_CR_OUT1 | UART011_CR_DTR | UART01x_CR_IIRLP | UART01x_CR_SIREN); /*Set all RO bit to 0*/

  /*Disable UART*/
  uart_cr &= ~(UART01x_CR_UARTEN);
  writel(uart_cr, pl011_port->membase + UART011_CR);

  /*Flush fifo*/
  writel(0, pl011_port->membase + UART011_LCRH);

  /*Set RX FIFO threshold to lowest and TX FIFO threshold to mid*/
  writel(UART011_IFLS_RX1_8 | UART011_IFLS_TX2_8, pl011_port->membase + UART011_IFLS);

  /*enable RX and TX*/
  uart_cr |= UART011_CR_RXE | UART011_CR_TXE;
  writel(uart_cr, pl011_port->membase + UART011_CR);

  /*Enable fifo and set to 8N1*/
  writel(UART01x_LCRH_FEN | UART01x_LCRH_WLEN_8, pl011_port->membase + UART011_LCRH);

  /*Enable UART*/
  uart_cr |= UART01x_CR_UARTEN;
  writel(uart_cr, pl011_port->membase + UART011_CR);

  /*Configure interrupts*/
  writel(UART011_OEIM | UART011_BEIM | UART011_FEIM | UART011_RTIM | UART011_RXIM, pl011_port->membase + UART011_IMSC);

  return 0;
}

static void pl011_raw_uart_stop_connection(struct generic_raw_uart *raw_uart)
{
  /*If uart is enabled, wait until it is not busy*/
  while ((readl(pl011_port->membase + UART011_CR) & UART01x_CR_UARTEN) && (readl(pl011_port->membase + UART01x_FR) & UART01x_FR_BUSY))
  {
    schedule();
  }

  writel(0, pl011_port->membase + UART011_CR); /*Disable UART*/

  writel(0, pl011_port->membase + UART011_IMSC); /*Disable interrupts*/

  free_irq(pl011_port->irq, raw_uart);
}

static void pl011_raw_uart_stop_tx(struct generic_raw_uart *raw_uart)
{
  unsigned long imsc;

  /*Diable TX interrupts*/
  imsc = readl(pl011_port->membase + UART011_IMSC);
  imsc &= ~(UART011_DSRMIM | UART011_DCDMIM | UART011_RIMIM); /*Set all RO bit to 0*/
  imsc &= ~(UART011_TXIM);                                    /*disable TX interrupt*/

  writel(imsc, pl011_port->membase + UART011_IMSC);
}

static bool pl011_raw_uart_isready_for_tx(struct generic_raw_uart *raw_uart)
{
  return !(readl(pl011_port->membase + UART01x_FR) & UART01x_FR_TXFF);
}

static void pl011_raw_uart_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len)
{
  writel(chr[index], pl011_port->membase + UART01x_DR);
}

static void pl011_raw_uart_init_tx(struct generic_raw_uart *raw_uart)
{
  unsigned long imsc;

  /*Clear TX interrupts*/
  writel(UART011_TXIC, pl011_port->membase + UART011_ICR);

  /*Enable TX interrupts*/
  imsc = readl(pl011_port->membase + UART011_IMSC);
  imsc &= ~(UART011_DSRMIM | UART011_DCDMIM | UART011_RIMIM); /*Set all RO bit to 0*/
  imsc |= UART011_TXIM;                                       /*enable TX interrupt*/

  writel(imsc, pl011_port->membase + UART011_IMSC);
}

static void pl011_raw_uart_rx_chars(struct generic_raw_uart *raw_uart)
{
  unsigned long status;
  unsigned long data;
  enum generic_raw_uart_rx_flags flags = GENERIC_RAW_UART_RX_STATE_NONE;

  while (1)
  {
    status = readl(pl011_port->membase + UART01x_FR);

    if (status & UART01x_FR_RXFE)
    {
      break;
    }

    data = readl(pl011_port->membase + UART01x_DR);

    /* Error handling */
    if (data & UART011_DR_BE)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_BREAK;
    }
    else
    {
      if (data & UART011_DR_PE)
      {
        flags |= GENERIC_RAW_UART_RX_STATE_PARITY;
      }
      if (data & UART011_DR_FE)
      {
        flags |= GENERIC_RAW_UART_RX_STATE_FRAME;
      }
      if (data & UART011_DR_OE)
      {
        flags |= GENERIC_RAW_UART_RX_STATE_OVERRUN;
      }
    }

    generic_raw_uart_handle_rx_char(raw_uart, flags, (unsigned char)data);
  }

  generic_raw_uart_rx_completed(raw_uart);
}

static irqreturn_t pl011_raw_uart_irq_handle(int irq, void *context)
{
  struct generic_raw_uart *raw_uart = context;

  u32 istat;

  istat = readl(pl011_port->membase + UART011_MIS);
  smp_rmb();

  /*Clear interrupts*/
  smp_wmb();

  writel(istat, pl011_port->membase + UART011_ICR);

  if (istat & (UART011_RXIS | UART011_RTIS))
  {
    pl011_raw_uart_rx_chars(raw_uart);
  }

  if (istat & UART011_TXIS)
  {
    generic_raw_uart_tx_queued(raw_uart);
  }

  return IRQ_HANDLED;
}

static struct raw_uart_driver pl011_raw_uart = {
    .owner = THIS_MODULE,
    .start_connection = pl011_raw_uart_start_connection,
    .stop_connection = pl011_raw_uart_stop_connection,
    .init_tx = pl011_raw_uart_init_tx,
    .isready_for_tx = pl011_raw_uart_isready_for_tx,
    .tx_chars = pl011_raw_uart_tx_chars,
    .stop_tx = pl011_raw_uart_stop_tx,
    .tx_chunk_size = TX_CHUNK_SIZE,
    .tx_bulktransfer_size = 1,
};

static int pl011_raw_uart_probe(struct platform_device *pdev)
{
  int err;
  struct device *dev = &pdev->dev;
  struct resource *ioresource;

  /* alloc private resources */
  pl011_port = kzalloc(sizeof(struct pl011_port_s), GFP_KERNEL);
  if (!pl011_port)
  {
    err = -ENOMEM;
    goto failed_inst_alloc;
  }

  /* Get mapbase and membase */
  ioresource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (ioresource)
  {
    pl011_port->mapbase = ioresource->start;
    pl011_port->membase = ioremap(ioresource->start, resource_size(ioresource));
  }
  else
  {
    dev_err(dev, "failed to get IO resource\n");
    err = -ENOENT;
    goto failed_get_resource;
  }

  /* get irq */
  pl011_port->irq = platform_get_irq(pdev, 0);
  if (pl011_port->irq <= 0)
  {
    dev_err(dev, "failed to get irq\n");
    err = -ENOENT;
    goto failed_get_resource;
  }

  /* get clock */
  pl011_port->clk = devm_clk_get(&pdev->dev, NULL);
  if (IS_ERR(pl011_port->clk))
  {
    dev_err(dev, "failed to get clock\n");
    err = PTR_ERR(pl011_port->clk);
    goto failed_get_clock;
  }
  clk_prepare_enable(pl011_port->clk);

  pl011_port->dev = dev;

  dev_info(dev, "Initialized pl011 device; mapbase=0x%08lx; irq=%lu; clockrate=%lu", pl011_port->mapbase, pl011_port->irq, clk_get_rate(pl011_port->clk));

  return 0;
failed_get_clock:
failed_get_resource:
  kfree(pl011_port);
failed_inst_alloc:
  return err;
}

static int pl011_raw_uart_remove(struct platform_device *pdev)
{
  clk_disable_unprepare(pl011_port->clk);
  kfree(pl011_port);
  return 0;
}

static struct of_device_id pl011_raw_uart_of_match[] = {
    {.compatible = "pivccu,pl011"},
    {/* sentinel */},
};

module_raw_uart_driver(MODULE_NAME, pl011_raw_uart, pl011_raw_uart_of_match);

MODULE_ALIAS("platform:pl011-raw-uart");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.10");
MODULE_DESCRIPTION("PL011 raw uart driver for communication of piVCCU with the HM-MOD-RPI-PCB and RPI-RF-MOD radio modules");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
