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

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/serial_reg.h>
#include <linux/delay.h>
#include <linux/version.h>

#include "stack_protector.include"

#include "generic_raw_uart.h"

#define MODULE_NAME "dw_apb_raw_uart"
#define TX_CHUNK_SIZE 9

#define DW_UART_USR 0x1f   /* UART Status register */
#define DW_UART_USR_BUSY 1 /* UART Busy */

#define DW_UART_SRR 0x22       /* Software reset register */
#define DW_UART_SRR_UR 1       /* reset UART */
#define DW_UART_SRR_RFR 1 << 1 /* Reset Receive FIFO */
#define DW_UART_SRR_XFR 1 << 2 /* Reset Xfer FIFo */

#define DW_UART_IER_PTIME 1 << 7 /* Programmable THRE Interrupt Mode Enable */

#define DW_UART_IIR_IID 0x0f /* nask for interrupt id */
#define DW_UART_IIR_CTO 0x0c /* character timeout */

static inline void dw_apb_raw_uart_writeb(int value, int offset);
static inline unsigned int dw_apb_raw_uart_readb(int offset);
static int dw_apb_raw_uart_start_connection(struct generic_raw_uart *raw_uart);
static void dw_apb_raw_uart_stop_connection(struct generic_raw_uart *raw_uart);
static void dw_apb_raw_uart_stop_tx(struct generic_raw_uart *raw_uart);
static bool dw_apb_raw_uart_isready_for_tx(struct generic_raw_uart *raw_uart);
static void dw_apb_raw_uart_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len);
static void dw_apb_raw_uart_init_tx(struct generic_raw_uart *raw_uart);
static void dw_apb_raw_uart_rx_chars(struct generic_raw_uart *raw_uart);
static irqreturn_t dw_apb_raw_uart_irq_handle(int irq, void *context);
static int dw_apb_raw_uart_probe(struct platform_device *pdev);
static int dw_apb_raw_uart_remove(struct platform_device *pdev);

struct dw_apb_port_s
{
  struct clk *sclk; /*Baud clock assigned to the UART device*/
  struct clk *pclk; /*System clock assigned to the UART device*/
  struct reset_control *rst;
  struct device *dev;    /*System device*/
  unsigned long mapbase; /*physical address of UART registers*/
  void __iomem *membase; /*logical address of UART registers*/
  int regshift;
  unsigned long irq; /*interrupt number*/
};

static struct dw_apb_port_s *dw_apb_port;

static inline void dw_apb_raw_uart_writeb(int value, int offset)
{
  writel(value, dw_apb_port->membase + (offset << dw_apb_port->regshift));
}

static inline unsigned int dw_apb_raw_uart_readb(int offset)
{
  return readl(dw_apb_port->membase + (offset << dw_apb_port->regshift));
}

static void dw_apb_raw_uart_write_lcr(int value)
{
  int tries = 1000;

  dw_apb_raw_uart_writeb(value, UART_LCR);

  if ((dw_apb_raw_uart_readb(UART_LCR) & ~UART_LCR_SPAR) == (value & ~UART_LCR_SPAR))
  {
    return;
  }

  while (tries--)
  {
    if ((dw_apb_raw_uart_readb(UART_LCR) & ~UART_LCR_SPAR) == (value & ~UART_LCR_SPAR))
    {
      return;
    }

    dw_apb_raw_uart_writeb(DW_UART_SRR_RFR | DW_UART_SRR_XFR, DW_UART_SRR);
    (void)dw_apb_raw_uart_readb(UART_RX);

    dw_apb_raw_uart_writeb(value, UART_LCR);
  }
}

static void dw_apb_raw_uart_init_uart(void)
{
  long rate;
  int divisor;

  // reset uart
  dw_apb_raw_uart_writeb(DW_UART_SRR_UR, DW_UART_SRR);
  msleep(200);

  // set bauclock
  rate = clk_round_rate(dw_apb_port->sclk, BAUD * 16);
  clk_disable_unprepare(dw_apb_port->sclk);
  clk_set_rate(dw_apb_port->sclk, rate);
  clk_prepare_enable(dw_apb_port->sclk);
  msleep(50);

  // set divisor and 8N1
  divisor = DIV_ROUND_CLOSEST(clk_get_rate(dw_apb_port->sclk), BAUD * 16);
  dw_apb_raw_uart_write_lcr(UART_LCR_DLAB);
  dw_apb_raw_uart_writeb(divisor & 0xff, UART_DLL);
  dw_apb_raw_uart_writeb((divisor >> 8) & 0xff, UART_DLM);
  dw_apb_raw_uart_write_lcr(UART_LCR_WLEN8);
}

static int dw_apb_raw_uart_start_connection(struct generic_raw_uart *raw_uart)
{
  int ret = 0;

  /* Disable interrupts */
  dw_apb_raw_uart_writeb(DW_UART_IER_PTIME, UART_IER);

  /* clear FIFO */
  dw_apb_raw_uart_writeb(0, UART_FCR);

  /*Register interrupt handler*/
  ret = request_irq(dw_apb_port->irq, dw_apb_raw_uart_irq_handle, 0, dev_name(dw_apb_port->dev), raw_uart);
  if (ret)
  {
    dev_err(dw_apb_port->dev, "irq could not be registered");
    return ret;
  }

  /* Enable interrupts */
  dw_apb_raw_uart_writeb(UART_IER_RDI | DW_UART_IER_PTIME, UART_IER);

  /* enable FIFO */
  dw_apb_raw_uart_writeb(UART_FCR_ENABLE_FIFO | UART_FCR_T_TRIG_01, UART_FCR);

  return 0;
}

static void dw_apb_raw_uart_stop_connection(struct generic_raw_uart *raw_uart)
{
  // wait until uart is not busy
  while (dw_apb_raw_uart_readb(DW_UART_USR) & DW_UART_USR_BUSY)
  {
    schedule();
  }

  /* disable interrupts */
  dw_apb_raw_uart_writeb(0, UART_IER);
  free_irq(dw_apb_port->irq, raw_uart);

  /* clear and disable fifo */
  dw_apb_raw_uart_writeb(0, UART_FCR);
}

static void dw_apb_raw_uart_stop_tx(struct generic_raw_uart *raw_uart)
{
  dw_apb_raw_uart_writeb(UART_IER_RDI | DW_UART_IER_PTIME, UART_IER);
}

static bool dw_apb_raw_uart_isready_for_tx(struct generic_raw_uart *raw_uart)
{
  return !(dw_apb_raw_uart_readb(UART_LSR) & UART_LSR_THRE); // FIFO not full
}

static void dw_apb_raw_uart_tx_chars(struct generic_raw_uart *raw_uart, unsigned char *chr, int index, int len)
{
  dw_apb_raw_uart_writeb(chr[index], UART_TX);
}

static void dw_apb_raw_uart_init_tx(struct generic_raw_uart *raw_uart)
{
  dw_apb_raw_uart_writeb(UART_IER_RDI | DW_UART_IER_PTIME | UART_IER_THRI, UART_IER);
}

static void dw_apb_raw_uart_rx_chars(struct generic_raw_uart *raw_uart)
{
  int status;
  int data;
  enum generic_raw_uart_rx_flags flags = GENERIC_RAW_UART_RX_STATE_NONE;

  status = dw_apb_raw_uart_readb(UART_LSR);

  while (status & (UART_LSR_DR | UART_LSR_BI))
  {
    /* Error handling */
    if (status & UART_LSR_BI)
    {
      flags |= GENERIC_RAW_UART_RX_STATE_BREAK;
    }
    else
    {
      if (status & UART_LSR_PE)
      {
        flags |= GENERIC_RAW_UART_RX_STATE_PARITY;
      }
      if (status & UART_LSR_FE)
      {
        flags |= GENERIC_RAW_UART_RX_STATE_FRAME;
      }
      if (status & UART_LSR_OE)
      {
        flags |= GENERIC_RAW_UART_RX_STATE_OVERRUN;
      }
    }

    data = dw_apb_raw_uart_readb(UART_RX);

    generic_raw_uart_handle_rx_char(raw_uart, flags, (unsigned char)data);

    status = dw_apb_raw_uart_readb(UART_LSR);
  }

  generic_raw_uart_rx_completed(raw_uart);
}

static irqreturn_t dw_apb_raw_uart_irq_handle(int irq, void *context)
{
  struct generic_raw_uart *raw_uart = context;
  int iid;
  int status;

  iid = dw_apb_raw_uart_readb(UART_IIR) & DW_UART_IIR_IID;

  switch (iid)
  {
  case DW_UART_IIR_CTO:
    status = dw_apb_raw_uart_readb(UART_LSR);
    if (!(status & (UART_LSR_DR | UART_LSR_BI)))
    {
      (void)dw_apb_raw_uart_readb(UART_RX);
    }
    // fall through

  case UART_IIR_RDI:
    dw_apb_raw_uart_rx_chars(raw_uart);
    break;

  case UART_IIR_THRI:
    generic_raw_uart_tx_queued(raw_uart);
    break;

  case UART_IIR_NO_INT:
    break;

  default:
    dev_err(dw_apb_port->dev, "unknown interrupt iid %02x", iid);
    break;
  }

  return IRQ_HANDLED;
}

static struct raw_uart_driver dw_apb_raw_uart = {
    .owner = THIS_MODULE,
    .start_connection = dw_apb_raw_uart_start_connection,
    .stop_connection = dw_apb_raw_uart_stop_connection,
    .init_tx = dw_apb_raw_uart_init_tx,
    .isready_for_tx = dw_apb_raw_uart_isready_for_tx,
    .tx_chars = dw_apb_raw_uart_tx_chars,
    .stop_tx = dw_apb_raw_uart_stop_tx,
    .tx_chunk_size = TX_CHUNK_SIZE,
    .tx_bulktransfer_size = 1,
};

static int dw_apb_raw_uart_probe(struct platform_device *pdev)
{
  int err;
  u32 val;
  struct device *dev = &pdev->dev;
  struct resource *ioresource;

  dw_apb_port = kzalloc(sizeof(struct dw_apb_port_s), GFP_KERNEL);
  if (!dw_apb_port)
  {
    err = -ENOMEM;
    goto failed_inst_alloc;
  }

  /* Get mapbase and membase */
  ioresource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
  if (ioresource)
  {
    dw_apb_port->mapbase = ioresource->start;
    dw_apb_port->membase = ioremap(ioresource->start, resource_size(ioresource));
  }
  else
  {
    dev_err(dev, "failed to get IO resource\n");
    err = -ENOENT;
    goto failed_get_resource;
  }

  /* get irq */
  dw_apb_port->irq = platform_get_irq(pdev, 0);
  if (dw_apb_port->irq <= 0)
  {
    dev_err(dev, "failed to get irq\n");
    err = -ENOENT;
    goto failed_get_resource;
  }

  /* get clock */
  dw_apb_port->sclk = devm_clk_get(&pdev->dev, "baudclk");
  if (IS_ERR(dw_apb_port->sclk) && PTR_ERR(dw_apb_port->sclk) != -EPROBE_DEFER)
    dw_apb_port->sclk = devm_clk_get(&pdev->dev, NULL);
  if (IS_ERR(dw_apb_port->sclk))
  {
    dev_err(dev, "failed to get sclk\n");
    err = PTR_ERR(dw_apb_port->sclk);
    goto failed_get_clock;
  }
  clk_prepare_enable(dw_apb_port->sclk);

  dw_apb_port->pclk = devm_clk_get(dev, "apb_pclk");
  if (IS_ERR(dw_apb_port->pclk) && PTR_ERR(dw_apb_port->pclk) == -EPROBE_DEFER)
  {
    dev_err(dev, "failed to get pclk\n");
    err = -EPROBE_DEFER;
    goto failed_get_pclock;
  }
  if (!IS_ERR(dw_apb_port->pclk))
  {
    clk_prepare_enable(dw_apb_port->pclk);
  }

  dw_apb_port->rst = devm_reset_control_get_optional(dev, NULL);
  if (IS_ERR(dw_apb_port->rst) && PTR_ERR(dw_apb_port->rst) == -EPROBE_DEFER)
  {
    err = -EPROBE_DEFER;
    goto failed_get_rst;
  }
  if (!IS_ERR(dw_apb_port->rst))
    reset_control_deassert(dw_apb_port->rst);

  err = device_property_read_u32(dev, "reg-shift", &val);
  if (!err)
    dw_apb_port->regshift = val;

  dw_apb_port->dev = dev;

  dw_apb_raw_uart_init_uart();

  dev_info(dev, "Initialized dw_apb device; mapbase=0x%08lx; irq=%lu; sclk rate=%lu; pclk rate=%ld",
           dw_apb_port->mapbase,
           dw_apb_port->irq,
           clk_get_rate(dw_apb_port->sclk),
           IS_ERR(dw_apb_port->pclk) ? -1 : clk_get_rate(dw_apb_port->pclk));

  return 0;

failed_get_rst:
  if (!IS_ERR(dw_apb_port->rst))
    reset_control_assert(dw_apb_port->rst);
failed_get_pclock:
  clk_disable_unprepare(dw_apb_port->sclk);
failed_get_clock:
failed_get_resource:
  kfree(dw_apb_port);
failed_inst_alloc:
  return err;
}

static int dw_apb_raw_uart_remove(struct platform_device *pdev)
{
  if (!IS_ERR(dw_apb_port->rst))
    reset_control_assert(dw_apb_port->rst);

  if (!IS_ERR(dw_apb_port->pclk))
    clk_disable_unprepare(dw_apb_port->pclk);

  clk_disable_unprepare(dw_apb_port->sclk);

  kfree(dw_apb_port);
  return 0;
}

static struct of_device_id dw_apb_raw_uart_of_match[] = {
    {.compatible = "pivccu,dw_apb"},
    {/* sentinel */},
};

module_raw_uart_driver(MODULE_NAME, dw_apb_raw_uart, dw_apb_raw_uart_of_match);

MODULE_ALIAS("platform:dw_apb-raw-uart");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.11");
MODULE_DESCRIPTION("dw_apb raw uart driver for communication of piVCCU with the HM-MOD-RPI-PCB and RPI-RF-MOD radio modules");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
