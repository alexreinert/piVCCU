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
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <crypto/hash.h>
#include "generic_raw_uart.h"

#include "stack_protector.include"

#define DRIVER_NAME "raw-uart"

#define MAX_DEVICES 5

#define CIRCBUF_SIZE 1024
#define CON_DATA_TX_BUF_SIZE 4096
#define PROC_DEBUG 1
#define MAX_CONNECTIONS 3
#define IOCTL_MAGIC 'u'
#define IOCTL_IOCSPRIORITY _IOW(IOCTL_MAGIC, 1, uint32_t) /* Set the priority for the current channel */
#define IOCTL_IOCGPRIORITY _IOR(IOCTL_MAGIC, 2, uint32_t) /* Get the priority for the current channel */
#define IOCTL_IOCRESET_RADIO_MODULE _IO(IOCTL_MAGIC, 0x81) /* Reset the radio module */
#define IOCTL_IOCGDEVINFO _IOW(IOCTL_MAGIC, 0x82, char[MAX_DEVICE_TYPE_LEN]) /* Get information about the raw uart device */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0))
#define _access_ok(__type, __addr, __size) access_ok(__addr, __size)
#else
#define _access_ok(__type, __addr, __size) access_ok(__type, __addr, __size)
#endif

static dev_t devid;
static struct class *class;

struct generic_raw_uart_instance
{
  spinlock_t lock_tx;                        /*TX lock for accessing tx_connection*/
  struct semaphore sem;                      /*semaphore for accessing this struct*/
  wait_queue_head_t readq;                   /*wait queue for read operations*/
  wait_queue_head_t writeq;                  /*wait queue for write operations*/
  struct circ_buf rxbuf;                     /*RX buffer*/
  int open_count;                            /*number of open connections*/
  bool connection_state;
  struct per_connection_data *tx_connection; /*connection which is currently sending*/
  struct termios termios;                    /*dummy termios for emulating ttyp ioctls*/

  int reset_pin;
  int red_pin;
  int green_pin;
  int blue_pin;

  int count_tx;          /*Statistic counter: Number of bytes transmitted*/
  int count_rx;          /*Statistic counter: Number of bytes received*/
  int count_brk;         /*Statistic counter: Number of break conditions received*/
  int count_parity;      /*Statistic counter: Number of parity errors*/
  int count_frame;       /*Statistic counter: Number of frame errors*/
  int count_overrun;     /*Statistic counter: Number of RX overruns in hardware FIFO*/
  int count_buf_overrun; /*Statistic counter: Number of RX overruns in user space buffer*/

  struct raw_uart_driver *driver;
  dev_t devid;
  struct cdev cdev;
  struct device *dev;
  struct device *parent;

  bool dump_traffic;
  unsigned char dump_tx_prefix[32];
  int dump_rxbuf_pos;
  unsigned char dump_rxbuf[32];
  unsigned char dump_rx_prefix[32];

  struct generic_raw_uart raw_uart;
};

struct per_connection_data
{
  unsigned char txbuf[CON_DATA_TX_BUF_SIZE];
  size_t tx_buf_length;   /*length of tx frame transmitted from userspace*/
  size_t tx_buf_index;    /*index into txbuf*/
  unsigned long priority; /*priority of the corresponding channel*/
  struct semaphore sem;   /*semaphore for accessing this struct.*/
};

static ssize_t generic_raw_uart_read(struct file *filep, char __user *buf, size_t count, loff_t *offset);
static ssize_t generic_raw_uart_write(struct file *filep, const char __user *buf, size_t count, loff_t *offset);
static int generic_raw_uart_open(struct inode *inode, struct file *filep);
static int generic_raw_uart_close(struct inode *inode, struct file *filep);
static unsigned int generic_raw_uart_poll(struct file *filep, poll_table *wait);
static long generic_raw_uart_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
static int generic_raw_uart_acquire_sender(struct generic_raw_uart_instance *instance, struct per_connection_data *conn);
static int generic_raw_uart_send_completed(struct generic_raw_uart_instance *instance, struct per_connection_data *conn);
static void generic_raw_uart_tx_queued_unlocked(struct generic_raw_uart_instance *instance);
#ifdef PROC_DEBUG
static int generic_raw_uart_proc_show(struct seq_file *m, void *v);
static int generic_raw_uart_proc_open(struct inode *inode, struct file *file);
#endif /*PROC_DEBUG*/

static int generic_raw_uart_get_device_type(struct generic_raw_uart_instance *instance, char *buf)
{
  if (instance->driver->get_device_type == 0)
  {
    return snprintf(buf, MAX_DEVICE_TYPE_LEN, "GPIO@%s", dev_name(instance->parent));
  }
  else
  {
    return instance->driver->get_device_type(&instance->raw_uart, buf);
  }
}

static struct file_operations generic_raw_uart_fops =
{
  .owner = THIS_MODULE,
  .llseek = no_llseek,
  .read = generic_raw_uart_read,
  .write = generic_raw_uart_write,
  .open = generic_raw_uart_open,
  .release = generic_raw_uart_close,
  .poll = generic_raw_uart_poll,
  .unlocked_ioctl = generic_raw_uart_ioctl,
  .compat_ioctl = generic_raw_uart_ioctl,
};

#ifdef PROC_DEBUG
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops generic_raw_uart_proc_fops =
{
  .proc_open = generic_raw_uart_proc_open,
  .proc_read = seq_read,
  .proc_lseek = seq_lseek,
  .proc_release = single_release,
};
#else
static const struct file_operations generic_raw_uart_proc_fops =
{
  .owner = THIS_MODULE,
  .open = generic_raw_uart_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};
#endif
#endif /*PROC_DEBUG*/

static ssize_t generic_raw_uart_read(struct file *filep, char __user *buf, size_t count, loff_t *offset)
{
  struct generic_raw_uart_instance *instance = container_of(filep->f_inode->i_cdev, struct generic_raw_uart_instance, cdev);
  int ret = 0;

  if (down_interruptible(&instance->sem))
  {
    ret = -ERESTARTSYS;
    goto exit;
  }

  while (!CIRC_CNT(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE)) /* Wait for data, if there's currently nothing to read */
  {
    up(&instance->sem);
    if (filep->f_flags & O_NONBLOCK)
    {
      ret = -EAGAIN;
      goto exit;
    }

    if (wait_event_interruptible(instance->readq, CIRC_CNT(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE)))
    {
      ret = -ERESTARTSYS;
      goto exit;
    }

    if (down_interruptible(&instance->sem))
    {
      ret = -ERESTARTSYS;
      goto exit;
    }
  }

  count = min((int)count, CIRC_CNT_TO_END(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE));
  if (copy_to_user(buf, instance->rxbuf.buf + instance->rxbuf.tail, count))
  {
    ret = -EFAULT;
    goto exit_sem;
  }
  ret = count;

  smp_mb();
  instance->rxbuf.tail += count;
  if (instance->rxbuf.tail >= CIRCBUF_SIZE)
  {
    instance->rxbuf.tail -= CIRCBUF_SIZE;
  }

exit_sem:
  up(&instance->sem);

exit:
  return ret;
}

static ssize_t generic_raw_uart_write(struct file *filep, const char __user *buf, size_t count, loff_t *offset)
{
  struct generic_raw_uart_instance *instance = container_of(filep->f_inode->i_cdev, struct generic_raw_uart_instance, cdev);
  struct per_connection_data *conn = filep->private_data;
  int ret = 0;

  if (down_interruptible(&conn->sem))
  {
    ret = -ERESTARTSYS;
    goto exit;
  }

  if (count > sizeof(conn->txbuf))
  {
    dev_err(instance->dev, "generic_raw_uart_write(): Error message size.");
    ret = -EMSGSIZE;
    goto exit_sem;
  }

  if (copy_from_user(conn->txbuf, buf, count))
  {
    dev_err(instance->dev, "generic_raw_uart_write(): Copy from user.");
    ret = -EFAULT;
    goto exit_sem;
  }

  conn->tx_buf_index = 0;
  conn->tx_buf_length = count;
  smp_wmb(); /*Wait until completion of all writes*/

  if (wait_event_interruptible(instance->writeq, generic_raw_uart_acquire_sender(instance, conn)))
  {
    ret = -ERESTARTSYS;
    goto exit_sem;
  }

  /*wait for sending to complete*/
  if (wait_event_interruptible(instance->writeq, generic_raw_uart_send_completed(instance, conn)))
  {
    ret = -ERESTARTSYS;
    goto exit_sem;
  }

  /*return number of characters actually sent*/
  ret = conn->tx_buf_index;

exit_sem:
  up(&conn->sem);

exit:
  return ret;
}

static int generic_raw_uart_reset_radio_module(struct generic_raw_uart_instance *instance, int max_open_count)
{
  int ret;

  if (down_interruptible(&instance->sem))
  {
    ret = -ERESTARTSYS;
    goto exit;
  }

  if (instance->open_count > max_open_count)
  {
    up(&instance->sem);
    ret = -EBUSY;
    goto exit_sem;
  }

  dev_info(instance->dev, "Reset radio module");

  if (instance->driver->reset_radio_module == 0)
  {
    if (instance->reset_pin != 0)
    {
      gpio_direction_output(instance->reset_pin, false);
      gpio_set_value(instance->reset_pin, 0);
      msleep(50);
      gpio_set_value(instance->reset_pin, 1);
      msleep(50);
      gpio_direction_input(instance->reset_pin);
      msleep(50);
      ret = 0;
    }
    else
    {
      ret = -ENOSYS;
    }
  }
  else
  {
    ret = instance->driver->reset_radio_module(&instance->raw_uart);
  }

exit_sem:
  up(&instance->sem);

exit:
  return 0;
}

static int generic_raw_uart_open(struct inode *inode, struct file *filep)
{
  int ret;
  struct per_connection_data *conn;
  struct generic_raw_uart_instance *instance = container_of(inode->i_cdev, struct generic_raw_uart_instance, cdev);

  if (instance == NULL)
  {
    return -ENODEV;
  }

  if (instance->driver->owner && !try_module_get(instance->driver->owner))
  {
    return -ENODEV;
  }

  /*Get semaphore*/
  if (down_interruptible(&instance->sem))
  {
    return -ERESTARTSYS;
  }

  /* check for the maximum number of connections */
  if (instance->open_count >= MAX_CONNECTIONS)
  {
    dev_err(instance->dev, "generic_raw_uart_open(): Too many open connections.");

    /*Release semaphore*/
    up(&instance->sem);

    return -EMFILE;
  }

  if (!instance->connection_state)
  {
    dev_err(instance->dev, "generic_raw_uart_open(): Tried to open disconnected device.");

    /*Release semaphore*/
    up(&instance->sem);

    return -ENODEV;
  }

  if (!instance->open_count) /*Enable HW for the first connection.*/
  {
    ret = instance->driver->start_connection(&instance->raw_uart);
    if (ret)
    {
      /*Release semaphore*/
      up(&instance->sem);
      return ret;
    }

    instance->rxbuf.head = instance->rxbuf.tail = 0;

    init_waitqueue_head(&instance->writeq);
    init_waitqueue_head(&instance->readq);
  }

  instance->open_count++;

  /*Release semaphore*/
  up(&instance->sem);

  conn = kmalloc(sizeof(struct per_connection_data), GFP_KERNEL);
  memset(conn, 0, sizeof(struct per_connection_data));

  sema_init(&conn->sem, 1);

  filep->private_data = (void *)conn;

  return 0;
}

static int generic_raw_uart_close(struct inode *inode, struct file *filep)
{
  struct per_connection_data *conn = filep->private_data;
  struct generic_raw_uart_instance *instance = container_of(inode->i_cdev, struct generic_raw_uart_instance, cdev);

  if (down_interruptible(&conn->sem))
  {
    return -ERESTARTSYS;
  }

  kfree(conn);

  if (down_interruptible(&instance->sem))
  {
    return -ERESTARTSYS;
  }

  if (instance->open_count)
  {
    instance->open_count--;
  }

  if (!instance->open_count)
  {
    instance->driver->stop_connection(&instance->raw_uart);
  }

  up(&instance->sem);

  module_put(instance->driver->owner);

  return 0;
}

static unsigned int generic_raw_uart_poll(struct file *filep, poll_table *wait)
{
  struct generic_raw_uart_instance *instance = container_of(filep->f_inode->i_cdev, struct generic_raw_uart_instance, cdev);
  struct per_connection_data *conn = filep->private_data;
  unsigned long lock_flags = 0;
  unsigned int mask = 0;

  poll_wait(filep, &instance->readq, wait);
  poll_wait(filep, &instance->writeq, wait);

  spin_lock_irqsave(&instance->lock_tx, lock_flags);
  if ((instance->tx_connection == NULL) || (instance->tx_connection->priority < conn->priority))
  {
    mask |= POLLOUT | POLLWRNORM;
  }
  spin_unlock_irqrestore(&instance->lock_tx, lock_flags);

  if (CIRC_CNT(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE) > 0)
  {
    mask |= POLLIN | POLLRDNORM;
  }

  return mask;
}

static long generic_raw_uart_ioctl(struct file *filep, unsigned int cmd, unsigned long arg)
{
  struct generic_raw_uart_instance *instance = container_of(filep->f_inode->i_cdev, struct generic_raw_uart_instance, cdev);
  struct per_connection_data *conn = filep->private_data;
  long ret = 0;
  unsigned long temp;
  char *buf;

  if (down_interruptible(&conn->sem))
  {
    return -ERESTARTSYS;
  }

  switch (cmd)
  {

  /* Set connection priority */
  case IOCTL_IOCSPRIORITY: /* Set: arg points to the value */
    if (_access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(unsigned long)))
    {
      ret = __get_user(temp, (unsigned long __user *)arg);
      if (!ret)
        conn->priority = temp;
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Get connection priority */
  case IOCTL_IOCGPRIORITY: /* Get: arg is pointer to result */
    if (_access_ok(VERIFY_READ, (void __user *)arg, sizeof(unsigned long)))
    {
      ret = __put_user(conn->priority, (unsigned long __user *)arg);
    }
    else
    {
      ret = -EFAULT;
    }
    break;

  case IOCTL_IOCRESET_RADIO_MODULE:
    ret = generic_raw_uart_reset_radio_module(instance, 1);
    break;

  case IOCTL_IOCGDEVINFO:
    if (_access_ok(VERIFY_READ, (void __user *)arg, MAX_DEVICE_TYPE_LEN))
    {
      buf = kmalloc(MAX_DEVICE_TYPE_LEN, GFP_KERNEL);
      if (buf)
      {
        ret = generic_raw_uart_get_device_type(instance, buf);

        if (ret > 0)
          ret = copy_to_user((void __user *)arg, buf, MAX_DEVICE_TYPE_LEN);

	kfree(buf);
      }
      else
      {
        ret  = -ENOMEM;
      }
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Emulated TTY ioctl: Get termios struct */
  case TCGETS:
    if (_access_ok(VERIFY_READ, (void __user *)arg, sizeof(struct termios)))
    {
      if (down_interruptible(&instance->sem))
      {
        ret = -ERESTARTSYS;
      }
      else
      {
        ret = copy_to_user((void __user *)arg, &instance->termios, sizeof(struct termios));
        up(&instance->sem);
      }
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Emulated TTY ioctl: Set termios struct */
  case TCSETS:
    if (_access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(struct termios)))
    {
      if (down_interruptible(&instance->sem))
      {
        ret = -ERESTARTSYS;
      }
      else
      {
        ret = copy_from_user(&instance->termios, (void __user *)arg, sizeof(struct termios));
        up(&instance->sem);
      }
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Emulated TTY ioctl: Get receive queue size */
  case TIOCINQ:
    if (_access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp)))
    {
      if (down_interruptible(&instance->sem))
      {
        ret = -ERESTARTSYS;
      }
      else
      {
        temp = CIRC_CNT(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE);
        up(&instance->sem);
        ret = __put_user(temp, (int __user *)arg);
      }
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Emulated TTY ioctl: Get send queue size */
  case TIOCOUTQ:
    if (_access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp)))
    {
      temp = 0;
      ret = __put_user(temp, (int __user *)arg);
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Emulated TTY ioctl: Exclusive use */
  case TIOCEXCL:
    break;

    /* Emulated TTY ioctl: Flush */
  case TCFLSH:
    break;

    /* Emulated TTY ioctl: Get states of modem control lines */
  case TIOCMGET:
    if (_access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp)))
    {
      temp = TIOCM_DSR | TIOCM_CD | TIOCM_CTS;
      ret = __put_user(temp, (int __user *)arg);
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Emulated TTY ioctl: Set states of modem control lines */
  case TIOCMSET:
    break;

  default:
    ret = -ENOTTY;
  }

  up(&conn->sem);
  return ret;
}

static int generic_raw_uart_acquire_sender(struct generic_raw_uart_instance *instance, struct per_connection_data *conn)
{
  int ret = 0;
  unsigned long lock_flags;
  int sender_idle;

  spin_lock_irqsave(&instance->lock_tx, lock_flags);
  sender_idle = instance->tx_connection == NULL;
  if (sender_idle || (instance->tx_connection->priority < conn->priority))
  {
    instance->tx_connection = conn;
    ret = 1;
    if (sender_idle)
    {
      instance->driver->init_tx(&instance->raw_uart);
      generic_raw_uart_tx_queued_unlocked(instance);
    }
    else
    {
      wake_up_interruptible(&instance->writeq);
    }
  }
  spin_unlock_irqrestore(&instance->lock_tx, lock_flags);
  return ret;
}

static int generic_raw_uart_send_completed(struct generic_raw_uart_instance *instance, struct per_connection_data *conn)
{
  int ret = 0;
  unsigned long lock_flags;

  spin_lock_irqsave(&instance->lock_tx, lock_flags);
  ret = instance->tx_connection != conn;
  spin_unlock_irqrestore(&instance->lock_tx, lock_flags);

  return ret;
}

void generic_raw_uart_handle_rx_char(struct generic_raw_uart *raw_uart, enum generic_raw_uart_rx_flags flags, unsigned char data)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;

  instance->count_rx++;

  if (flags & GENERIC_RAW_UART_RX_STATE_BREAK)
  {
    instance->count_brk++;
  }
  else
  {
    if (flags & GENERIC_RAW_UART_RX_STATE_PARITY)
    {
      instance->count_parity++;
    }
    if (flags & GENERIC_RAW_UART_RX_STATE_FRAME)
    {
      instance->count_frame++;
    }
    if (flags & GENERIC_RAW_UART_RX_STATE_OVERRUN)
    {
      instance->count_overrun++;
    }

    if (instance->dump_traffic)
    {
      instance->dump_rxbuf[instance->dump_rxbuf_pos++] = data;
      if (instance->dump_rxbuf_pos == sizeof(instance->dump_rxbuf))
      {
        print_hex_dump(KERN_INFO, instance->dump_rx_prefix, DUMP_PREFIX_NONE, 32, 1, instance->dump_rxbuf, sizeof(instance->dump_rxbuf), false);
        instance->dump_rxbuf_pos = 0;
      }
    }

    if (CIRC_SPACE(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE))
    {
      instance->rxbuf.buf[instance->rxbuf.head] = data;
      smp_wmb();

      if (++(instance->rxbuf.head) >= CIRCBUF_SIZE)
      {
        instance->rxbuf.head = 0;
      }
    }
    else
    {
      dev_err(instance->dev, "generic_raw_uart_handle_rx_char(): rx fifo full.");
    }
  }
}
EXPORT_SYMBOL(generic_raw_uart_handle_rx_char);

void generic_raw_uart_rx_completed(struct generic_raw_uart *raw_uart)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;

  if (instance->dump_traffic)
  {
    print_hex_dump(KERN_INFO, instance->dump_rx_prefix, DUMP_PREFIX_NONE, 32, 1, instance->dump_rxbuf, instance->dump_rxbuf_pos, false);
    instance->dump_rxbuf_pos = 0;
  }

  wake_up_interruptible(&instance->readq);
}
EXPORT_SYMBOL(generic_raw_uart_rx_completed);

void generic_raw_uart_tx_queued(struct generic_raw_uart *raw_uart)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;

  spin_lock(&instance->lock_tx);
  generic_raw_uart_tx_queued_unlocked(instance);
  spin_unlock(&instance->lock_tx);
}
EXPORT_SYMBOL(generic_raw_uart_tx_queued);

static inline void generic_raw_uart_tx_queued_unlocked(struct generic_raw_uart_instance *instance)
{
  int tx_count = 0;
  int bulksize = 0;

  while ((tx_count < instance->driver->tx_chunk_size) && (instance->driver->isready_for_tx(&instance->raw_uart)) &&
         (instance->tx_connection != NULL) && (instance->tx_connection->tx_buf_index < instance->tx_connection->tx_buf_length))
  {
    bulksize = min(instance->driver->tx_bulktransfer_size, (int)(instance->tx_connection->tx_buf_length - instance->tx_connection->tx_buf_index));

    if (instance->dump_traffic)
      print_hex_dump(KERN_INFO, instance->dump_tx_prefix, DUMP_PREFIX_NONE, 32, 1, &instance->tx_connection->txbuf[instance->tx_connection->tx_buf_index], bulksize, false);

    instance->driver->tx_chars(&instance->raw_uart, instance->tx_connection->txbuf, instance->tx_connection->tx_buf_index, bulksize);
    instance->tx_connection->tx_buf_index += bulksize;
    smp_wmb();
    tx_count += bulksize;
#ifdef PROC_DEBUG
    instance->count_tx += bulksize;
#endif /*PROC_DEBUG*/
  }

  if ((instance->tx_connection != NULL) && (instance->tx_connection->tx_buf_index >= instance->tx_connection->tx_buf_length))
  {
    instance->driver->stop_tx(&instance->raw_uart);
    instance->tx_connection = NULL;
    smp_wmb();
    wake_up_interruptible(&instance->writeq);
  }
}

#ifdef PROC_DEBUG
static int generic_raw_uart_proc_show(struct seq_file *m, void *v)
{
  struct generic_raw_uart_instance *instance = m->private;

  seq_printf(m, "open_count=%d\n", instance->open_count);
  seq_printf(m, "count_tx=%d\n", instance->count_tx);
  seq_printf(m, "count_rx=%d\n", instance->count_rx);
  seq_printf(m, "count_brk=%d\n", instance->count_brk);
  seq_printf(m, "count_parity=%d\n", instance->count_parity);
  seq_printf(m, "count_frame=%d\n", instance->count_frame);
  seq_printf(m, "count_overrun=%d\n", instance->count_overrun);
  seq_printf(m, "rxbuf_size=%d\n", CIRC_CNT(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE));
  seq_printf(m, "rxbuf_head=%d\n", instance->rxbuf.head);
  seq_printf(m, "rxbuf_tail=%d\n", instance->rxbuf.tail);

  return 0;
}

static int generic_raw_uart_proc_open(struct inode *inode, struct file *file)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 17, 0))
  struct generic_raw_uart_instance *instance = pde_data(inode);
#else
  struct generic_raw_uart_instance *instance = PDE_DATA(inode);
#endif

  return single_open(file, generic_raw_uart_proc_show, instance);
}
#endif /*PROC_DEBUG*/

const char *generic_raw_uart_get_pin_label(enum generic_raw_uart_pin pin)
{
  switch (pin)
  {
  case GENERIC_RAW_UART_PIN_BLUE:
    return "pivccu,blue_pin";
  case GENERIC_RAW_UART_PIN_GREEN:
    return "pivccu,green_pin";
  case GENERIC_RAW_UART_PIN_RED:
    return "pivccu,red_pin";
  case GENERIC_RAW_UART_PIN_RESET:
    return "pivccu,reset_pin";
  case GENERIC_RAW_UART_PIN_ALT_RESET:
    return "pivccu,alt_reset_pin";
  }
  return 0;
}

int generic_raw_uart_get_gpio_pin_number(struct generic_raw_uart_instance *instance, struct device *dev, enum generic_raw_uart_pin pin)
{
  int res;

  if (instance->driver->get_gpio_pin_number == 0)
  {
    struct fwnode_handle *fwnode = dev_fwnode(dev);
    const char *label = generic_raw_uart_get_pin_label(pin);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0))
    struct gpio_desc *gpiod = fwnode_get_named_gpiod(fwnode, label, 0, GPIOD_ASIS, label);
#else
    struct gpio_desc *gpiod = fwnode_get_named_gpiod(fwnode, label);
#endif

    if (IS_ERR_OR_NULL(gpiod))
      return 0;

    res = desc_to_gpio(gpiod);

    gpiod_put(gpiod);

    return res;
  }
  else
  {
    return instance->driver->get_gpio_pin_number(&instance->raw_uart, pin);
  }
}

static ssize_t reset_radio_module_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  bool val;
  int ret;

  if (!kstrtobool(strim((char *)buf), &val) && val)
  {
    ret = generic_raw_uart_reset_radio_module(instance, 0);
    return ret == 0 ? count : ret;
  }
  else
  {
    return -EINVAL;
  }
}
static DEVICE_ATTR_WO(reset_radio_module);

static ssize_t red_gpio_pin_show(struct device *dev, struct device_attribute *attr, char *page)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  return sprintf(page, "%d\n", instance->red_pin);
}
static DEVICE_ATTR_RO(red_gpio_pin);
static ssize_t green_gpio_pin_show(struct device *dev, struct device_attribute *attr, char *page)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  return sprintf(page, "%d\n", instance->green_pin);
}
static DEVICE_ATTR_RO(green_gpio_pin);
static ssize_t blue_gpio_pin_show(struct device *dev, struct device_attribute *attr, char *page)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  return sprintf(page, "%d\n", instance->blue_pin);
}
static DEVICE_ATTR_RO(blue_gpio_pin);

static ssize_t device_type_show(struct device *dev, struct device_attribute *attr, char *page)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  int ret = generic_raw_uart_get_device_type(instance, page);
  if (ret > 0)
  {
    page[ret++] = '\n';
    page[ret] = 0;
  }
  return ret;
}
static DEVICE_ATTR_RO(device_type);

static ssize_t dump_traffic_show(struct device *dev, struct device_attribute *attr, char *page)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  return sprintf(page, instance->dump_traffic ? "on" : "off");
}
static ssize_t dump_traffic_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  bool val;

  if (!kstrtobool(strim((char *)buf), &val))
  {
    instance->dump_traffic = val;
    dev_info(instance->dev, val ? "Enabled traffic dumping to kernel log" : "Disabled traffic dumping to kernel log");
    return count;
  }
  else
  {
    return -EINVAL;
  }
}
static DEVICE_ATTR_RW(dump_traffic);

static ssize_t open_count_show(struct device *dev, struct device_attribute *attr, char *page)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  return sprintf(page, "%d\n", instance->open_count);
}
static DEVICE_ATTR_RO(open_count);

static ssize_t connection_state_show(struct device *dev, struct device_attribute *attr, char *page)
{
  struct generic_raw_uart_instance *instance = dev_get_drvdata(dev);
  return sprintf(page, "%d\n", instance->connection_state);
}
static DEVICE_ATTR_RO(connection_state);

static spinlock_t active_devices_lock;
static bool active_devices[MAX_DEVICES] = {false};

static int __match_i2c_client_by_address(struct device *dev, void *addrp)
{
  struct i2c_client *client = i2c_verify_client(dev);
  int addr = *(int *)addrp;
  return (client && client->addr == addr) ? 1 : 0;
}

static struct i2c_client *i2c_find_client(struct i2c_adapter *adapter, int addr)
{
  struct device *child;
  child = device_find_child(&adapter->dev, &addr, __match_i2c_client_by_address);

  if (child)
  {
    put_device(child);
    return i2c_verify_client(child);
  }

  return NULL;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 5, 0))
static inline bool i2c_client_has_driver(struct i2c_client *client)
{
	return !IS_ERR_OR_NULL(client) && client->dev.driver;
}
#endif

int generic_raw_uart_probe_rtc_device(struct device *dev, bool *rtc_detected)
{
  int err = 0;

#if defined(CONFIG_OF) && (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 18, 0))
  struct device_node *rtc_of_node;
  struct i2c_adapter *rtc_adapter;
  struct i2c_client *rtc_client;
  struct i2c_board_info rtc_i2c_info;
  char rtc_module_alias[I2C_NAME_SIZE + 4] = "i2c:";

  *rtc_detected = false;

  if (dev->of_node)
  {
    rtc_of_node = of_parse_phandle(dev->of_node, "pivccu,rtc", 0);
    if (rtc_of_node)
    {
      of_i2c_get_board_info(dev, rtc_of_node, &rtc_i2c_info);

      rtc_adapter = of_get_i2c_adapter_by_node(rtc_of_node->parent);
      if (rtc_adapter)
      {
        rtc_client = i2c_find_client(rtc_adapter, rtc_i2c_info.addr);

        if (!rtc_client)
        {
          dev_err(dev, "Configured RTC device is not yet initialized");
          err = -EPROBE_DEFER;
        }
        else
        {
	  if (!i2c_client_has_driver(rtc_client))
          {
            dev_info(dev, "Missing I2C driver of rtc device, trying to load");

            if (of_modalias_node(rtc_of_node, rtc_module_alias + 4, sizeof(rtc_module_alias) - 4) == 0)
            {
              dev_info(dev, "Requesting module %s", rtc_module_alias);
              request_module(rtc_module_alias);
            }

	    i2c_unregister_device(rtc_client);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
            rtc_client = i2c_new_client_device(rtc_adapter, &rtc_i2c_info);
#else
            rtc_client = i2c_new_device(rtc_adapter, &rtc_i2c_info);
#endif
          }

          if (i2c_client_has_driver(rtc_client))
          {
            *rtc_detected = true;
          }

          err = 0;
        }

        i2c_put_adapter(rtc_adapter);
      }
      else
      {
        dev_err(dev, "I2C adapter of configured RTC device is not yet initialized");
        err = -EPROBE_DEFER;
      }

      of_node_put(rtc_of_node);
    }
  }
#else
  *rtc_detected = false;
#endif

  return err;
}

struct generic_raw_uart *generic_raw_uart_probe(struct device *dev, struct raw_uart_driver *drv, void *driver_data)
{
  int err;
  int i;
  int dev_no = MAX_DEVICES;
  struct generic_raw_uart_instance *instance;
  unsigned long flags;
  bool use_alt_reset_pin = false;
  char buf[MAX_DEVICE_TYPE_LEN] = { 0 };

  err = generic_raw_uart_probe_rtc_device(dev, &use_alt_reset_pin);
  if (err != 0)
  {
    goto failed_probe_rtc;
  }

  if (use_alt_reset_pin)
  {
    dev_info(dev, "Detected RPI-RF-MOD, using alternative reset pin");
  }

  spin_lock_irqsave(&active_devices_lock, flags);
  for (i = 0; i < MAX_DEVICES; i++)
  {
    if (!active_devices[i])
    {
      dev_no = i;
      active_devices[i] = true;
      break;
    }
  }
  spin_unlock_irqrestore(&active_devices_lock, flags);

  if (dev_no >= MAX_DEVICES)
  {
    err = -EFAULT;
    goto failed_inst_alloc;
  }

  instance = kzalloc(sizeof(struct generic_raw_uart_instance), GFP_KERNEL);
  if (!instance)
  {
    err = -ENOMEM;
    goto failed_inst_alloc;
  }

  instance->raw_uart.private = instance;
  instance->raw_uart.driver_data = driver_data;
  instance->raw_uart.dev_number = dev_no;
  instance->driver = drv;

  instance->devid = MKDEV(MAJOR(devid), MINOR(devid) + dev_no);

  cdev_init(&instance->cdev, &generic_raw_uart_fops);
  instance->cdev.owner = THIS_MODULE;

  err = cdev_add(&instance->cdev, instance->devid, 1);
  if (err != 0)
  {
    dev_err(dev, "unable to register device");
    goto failed_cdev_add;
  }

  if (dev_no == 0)
    instance->dev = device_create(class, NULL, instance->devid, NULL, DRIVER_NAME);
  else
    instance->dev = device_create(class, NULL, instance->devid, NULL, DRIVER_NAME "%d", dev_no);

  if (IS_ERR(instance->dev))
  {
    err = PTR_ERR(instance->dev);
    goto failed_device_create;
  }

  dev_set_drvdata(instance->dev, instance);

  instance->parent = dev;

  instance->reset_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, use_alt_reset_pin ? GENERIC_RAW_UART_PIN_ALT_RESET : GENERIC_RAW_UART_PIN_RESET);

  if (instance->reset_pin != 0)
  {
    gpio_request(instance->reset_pin, "pivccu:reset");
  }
  else if (instance->driver->reset_radio_module == 0)
  {
    dev_info(dev, "No valid reset pin configured");
  }

  instance->red_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, GENERIC_RAW_UART_PIN_RED);
  instance->green_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, GENERIC_RAW_UART_PIN_GREEN);
  instance->blue_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, GENERIC_RAW_UART_PIN_BLUE);

  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_device_type.attr);

  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_red_gpio_pin.attr);
  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_green_gpio_pin.attr);
  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_blue_gpio_pin.attr);

  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_reset_radio_module.attr);

  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_dump_traffic.attr);
  snprintf(instance->dump_tx_prefix, sizeof(instance->dump_tx_prefix), "%s %s: TX: ", dev_driver_string(instance->dev), dev_name(instance->dev));
  snprintf(instance->dump_rx_prefix, sizeof(instance->dump_rx_prefix), "%s %s: RX: ", dev_driver_string(instance->dev), dev_name(instance->dev));

  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_open_count.attr);
  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_connection_state.attr);

  sema_init(&instance->sem, 1);
  spin_lock_init(&instance->lock_tx);
  init_waitqueue_head(&instance->readq);
  init_waitqueue_head(&instance->writeq);

  instance->rxbuf.buf = kmalloc(CIRCBUF_SIZE, GFP_KERNEL);

#ifdef PROC_DEBUG
  proc_create_data(dev_name(instance->dev), 0444, NULL, &generic_raw_uart_proc_fops, instance);
#endif

  generic_raw_uart_reset_radio_module(instance, 0);

  generic_raw_uart_get_device_type(instance, buf);
  dev_info(instance->dev, "Registered new raw-uart device using underlying device %s.", buf);

  instance->connection_state = true;

  return &instance->raw_uart;

failed_device_create:
  cdev_del(&instance->cdev);
failed_cdev_add:
  unregister_chrdev_region(instance->devid, 1);
  kfree(instance);
failed_inst_alloc:
failed_probe_rtc:
  return ERR_PTR(err);
}
EXPORT_SYMBOL(generic_raw_uart_probe);

int generic_raw_uart_set_connection_state(struct generic_raw_uart *raw_uart, bool state)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;

  instance->connection_state = state;
  sysfs_notify(&instance->dev->kobj, NULL, "connection_state");

  return 0;
}
EXPORT_SYMBOL(generic_raw_uart_set_connection_state);

int generic_raw_uart_remove(struct generic_raw_uart *raw_uart)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;
  unsigned long flags;

  generic_raw_uart_set_connection_state(raw_uart, false);

#ifdef PROC_DEBUG
  remove_proc_entry(dev_name(instance->dev), NULL);
#endif

  sysfs_remove_file(&instance->dev->kobj, &dev_attr_device_type.attr);

  sysfs_remove_file(&instance->dev->kobj, &dev_attr_red_gpio_pin.attr);
  sysfs_remove_file(&instance->dev->kobj, &dev_attr_green_gpio_pin.attr);
  sysfs_remove_file(&instance->dev->kobj, &dev_attr_blue_gpio_pin.attr);

  sysfs_remove_file(&instance->dev->kobj, &dev_attr_reset_radio_module.attr);

  sysfs_remove_file(&instance->dev->kobj, &dev_attr_dump_traffic.attr);

  sysfs_remove_file(&instance->dev->kobj, &dev_attr_open_count.attr);
  sysfs_remove_file(&instance->dev->kobj, &dev_attr_connection_state.attr);

  if (instance->reset_pin != 0)
  {
    gpio_free(instance->reset_pin);
  }

  device_destroy(class, instance->devid);
  cdev_del(&instance->cdev);

  spin_lock_irqsave(&active_devices_lock, flags);
  active_devices[instance->raw_uart.dev_number] = false;
  spin_unlock_irqrestore(&active_devices_lock, flags);

  kfree(instance);

  return 0;
}
EXPORT_SYMBOL(generic_raw_uart_remove);

static int __init generic_raw_uart_init(void)
{
  int err = alloc_chrdev_region(&devid, 0, MAX_DEVICES, DRIVER_NAME);
  if (err != 0)
    return err;

  class = class_create(THIS_MODULE, DRIVER_NAME);
  if (IS_ERR(class))
    return PTR_ERR(class);

  spin_lock_init(&active_devices_lock);

  return 0;
}

static void __exit generic_raw_uart_exit(void)
{
  unregister_chrdev_region(devid, MAX_DEVICES);
  class_destroy(class);
}

module_init(generic_raw_uart_init);
module_exit(generic_raw_uart_exit);

static int generic_raw_uart_set_dummy_rx8130_loader(const char *val, const struct kernel_param *kp)
{
  bool load;

  if (!kstrtobool(val, &load) && !load)
  {
    return -EINVAL;
  }

  return request_module("dummy_rx8130");
}

static const struct kernel_param_ops generic_raw_uart_set_dummy_rx8130_loader_param_ops = {
    .set = generic_raw_uart_set_dummy_rx8130_loader,
};

module_param_cb(load_dummy_rx8130_module, &generic_raw_uart_set_dummy_rx8130_loader_param_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(load_dummy_rx8130_module, "Loads the dummy_rx8130 module");

struct sdesc {
  struct shash_desc shash;
  char ctx[];
};

static struct sdesc *init_sdesc(struct crypto_shash *alg)
{
  struct sdesc *sdesc;
  int size;

  size = sizeof(struct shash_desc) + crypto_shash_descsize(alg);
  sdesc = kmalloc(size, GFP_KERNEL);
  if (!sdesc)
    return ERR_PTR(-ENOMEM);
  sdesc->shash.tfm = alg;
  return sdesc;
}

#include "devkey.inc"

bool generic_raw_uart_verify_dkey(struct device *dev, unsigned char *dkey, int dkey_len, unsigned char *skey, uint32_t *pkey, int bytes)
{
  unsigned char hkey[32];
  unsigned char prefix[32];
  struct sdesc *sdesc;
  struct crypto_shash *shash;

  switch (bytes)
  {
    case 16:
      shash = crypto_alloc_shash("md5", 0, 0);
      break;
    case 32:
      shash = crypto_alloc_shash("sha256", 0, 0);
      break;
    default:
      return false;
  }

  if(IS_ERR(shash))
    return false;

  sdesc = init_sdesc(shash);
  if (IS_ERR(sdesc))
  {
    crypto_free_shash(shash);
    return false;
  }

  crypto_shash_digest(&sdesc->shash, dkey, dkey_len, hkey);

  kfree(sdesc);
  crypto_free_shash(shash);

  snprintf(prefix, sizeof(prefix), "%s %s: DKey: ", dev_driver_string(dev), dev_name(dev));
  print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_NONE, 32, min(dkey_len, 8), dkey, dkey_len, false);
  snprintf(prefix, sizeof(prefix), "%s %s: HKey: ", dev_driver_string(dev), dev_name(dev));
  print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_NONE, 32, 8, hkey, bytes, false);
  snprintf(prefix, sizeof(prefix), "%s %s: SKey: ", dev_driver_string(dev), dev_name(dev));
  print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_NONE, 32, 8, skey, bytes * 2, false);
  snprintf(prefix, sizeof(prefix), "%s %s: PKey: ", dev_driver_string(dev), dev_name(dev));
  print_hex_dump(KERN_INFO, prefix, DUMP_PREFIX_NONE, 32, 8, pkey, bytes * 2, false);

  return verify_device_key(pkey, hkey, skey, bytes);
}
EXPORT_SYMBOL(generic_raw_uart_verify_dkey);

MODULE_ALIAS("platform:generic-raw-uart");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.26");
MODULE_DESCRIPTION("generic raw uart driver for communication of debmatic and piVCCU with the HM-MOD-RPI-PCB and RPI-RF-MOD radio modules");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");

