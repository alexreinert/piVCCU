/*-----------------------------------------------------------------------------
 * Copyright (c) 2019 by Alexander Reinert
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

#include "generic_raw_uart.h"

#define DRIVER_NAME "raw-uart"

#define MAX_DEVICES 5

#define CIRCBUF_SIZE 1024
#define CON_DATA_TX_BUF_SIZE 4096
#define PROC_DEBUG  1
#define IOCTL_MAGIC 'u'
#define IOCTL_MAXNR 2
#define MAX_CONNECTIONS 3
#define IOCTL_IOCSPRIORITY _IOW(IOCTL_MAGIC,  1, uint32_t) /* Set the priority for the current channel */
#define IOCTL_IOCGPRIORITY _IOR(IOCTL_MAGIC,  2, uint32_t) /* Get the priority for the current channel */

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
  #define _access_ok(__type, __addr, __size) access_ok(__addr, __size)
#else
  #define _access_ok(__type, __addr, __size) access_ok(__type, __addr, __size)
#endif

static dev_t devid;
static struct class *class;

struct generic_raw_uart_instance
{
  spinlock_t lock_tx;                         /*TX lock for accessing tx_connection*/
  struct semaphore sem;                       /*semaphore for accessing this struct*/
  wait_queue_head_t readq;                    /*wait queue for read operations*/
  wait_queue_head_t writeq;                   /*wait queue for write operations*/
  struct circ_buf rxbuf;                      /*RX buffer*/
  int open_count;                             /*number of open connections*/
  struct per_connection_data *tx_connection;  /*connection which is currently sending*/
  struct termios termios;                     /*dummy termios for emulating ttyp ioctls*/

  int reset_pin;
  int red_pin;
  int green_pin;
  int blue_pin;

  int count_tx;                               /*Statistic counter: Number of bytes transmitted*/
  int count_rx;                               /*Statistic counter: Number of bytes received*/
  int count_brk;                              /*Statistic counter: Number of break conditions received*/
  int count_parity;                           /*Statistic counter: Number of parity errors*/
  int count_frame;                            /*Statistic counter: Number of frame errors*/
  int count_overrun;                          /*Statistic counter: Number of RX overruns in hardware FIFO*/
  int count_buf_overrun;                      /*Statistic counter: Number of RX overruns in user space buffer*/

  struct raw_uart_driver *driver;
  dev_t devid;
  struct cdev cdev;
  struct device *dev;

  struct generic_raw_uart raw_uart;
};

struct per_connection_data
{
  unsigned char txbuf[CON_DATA_TX_BUF_SIZE];
  size_t tx_buf_length;                 /*length of tx frame transmitted from userspace*/
  size_t tx_buf_index;                  /*index into txbuf*/
  unsigned long priority;               /*priority of the corresponding channel*/
  struct semaphore sem;                 /*semaphore for accessing this struct.*/
};

static ssize_t generic_raw_uart_read(struct file *filep, char __user *buf, size_t count, loff_t *offset);
static ssize_t generic_raw_uart_write(struct file *filep, const char __user *buf, size_t count, loff_t *offset);
static int generic_raw_uart_open(struct inode *inode, struct file *filep);
static int generic_raw_uart_close(struct inode *inode, struct file *filep);
static unsigned int generic_raw_uart_poll(struct file* filep, poll_table* wait);
static long generic_raw_uart_ioctl(struct file *filep, unsigned int cmd, unsigned long arg);
static int generic_raw_uart_acquire_sender(struct generic_raw_uart_instance *instance, struct per_connection_data *conn);
static int generic_raw_uart_send_completed(struct generic_raw_uart_instance *instance, struct per_connection_data *conn);
static void generic_raw_uart_tx_queued_unlocked(struct generic_raw_uart_instance *instance);
#ifdef PROC_DEBUG
static int generic_raw_uart_proc_show(struct seq_file *m, void *v);
static int generic_raw_uart_proc_open(struct inode *inode, struct  file *file);
#endif /*PROC_DEBUG*/

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
static const struct file_operations generic_raw_uart_proc_fops =
{
  .owner = THIS_MODULE,
  .open = generic_raw_uart_proc_open,
  .read = seq_read,
  .llseek = seq_lseek,
  .release = single_release,
};
#endif /*PROC_DEBUG*/

static ssize_t generic_raw_uart_read(struct file *filep, char __user *buf, size_t count, loff_t *offset)
{
  struct generic_raw_uart_instance *instance = container_of(filep->f_inode->i_cdev, struct generic_raw_uart_instance, cdev);
  int ret = 0;

  if( down_interruptible( &instance->sem ))
  {
    ret = -ERESTARTSYS;
    goto exit;
  }

  while( !CIRC_CNT( instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE )) /* Wait for data, if there's currently nothing to read */
  {
    up( &instance->sem );
    if( filep->f_flags & O_NONBLOCK )
    {
      ret = -EAGAIN;
      goto exit;
    }

    if( wait_event_interruptible(instance->readq, CIRC_CNT(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE )) )
    {
      ret = -ERESTARTSYS;
      goto exit;
    }

    if( down_interruptible( &instance->sem ))
    {
      ret = -ERESTARTSYS;
      goto exit;
    }
  }

  count = min( (int)count, CIRC_CNT_TO_END(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE) );
  if( copy_to_user(buf, instance->rxbuf.buf + instance->rxbuf.tail, count) )
  {
    ret = -EFAULT;
    goto exit_sem;
  }
  ret = count;

  smp_mb();
  instance->rxbuf.tail += count;
  if( instance->rxbuf.tail >= CIRCBUF_SIZE )
  {
    instance->rxbuf.tail -= CIRCBUF_SIZE;
  }

exit_sem:
  up( &instance->sem );

exit:
  return ret;
}

static ssize_t generic_raw_uart_write(struct file *filep, const char __user *buf, size_t count, loff_t *offset)
{
  struct generic_raw_uart_instance *instance = container_of(filep->f_inode->i_cdev, struct generic_raw_uart_instance, cdev);
  struct per_connection_data *conn = filep->private_data;
  int ret = 0;

  if( down_interruptible(&conn->sem) )
  {
    ret = -ERESTARTSYS;
    goto exit;
  }

  if( count > sizeof(conn->txbuf)  )
  {
    dev_err(instance->dev, "generic_raw_uart_write(): Error message size.");
    ret = -EMSGSIZE;
    goto exit_sem;
  }

  if( copy_from_user(conn->txbuf, buf, count) )
  {
    dev_err(instance->dev, "generic_raw_uart_write(): Copy from user.");
    ret = -EFAULT;
    goto exit_sem;
  }

  conn->tx_buf_index = 0;
  conn->tx_buf_length = count;
  smp_wmb();  /*Wait until completion of all writes*/

  if( wait_event_interruptible(instance->writeq, generic_raw_uart_acquire_sender(instance, conn)) )
  {
    ret = -ERESTARTSYS;
    goto exit_sem;
  }

  /*wait for sending to complete*/
  if( wait_event_interruptible(instance->writeq, generic_raw_uart_send_completed(instance, conn)) )
  {
    ret = -ERESTARTSYS;
    goto exit_sem;
  }

  /*return number of characters actually sent*/
  ret = conn->tx_buf_index;

exit_sem:
  up( &conn->sem );

exit:
  return ret;
}

static void generic_raw_uart_reset_radio_module(struct generic_raw_uart_instance *instance)
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
  }
}

static int generic_raw_uart_open(struct inode *inode, struct file *filep)
{
  int ret;
  struct per_connection_data *conn;
  struct generic_raw_uart_instance *instance = container_of(inode->i_cdev, struct generic_raw_uart_instance, cdev);

  if( instance == NULL )
  {
    return -ENODEV;
  }

  /*Get semaphore*/
  if( down_interruptible(&instance->sem) )
  {
    return -ERESTARTSYS;
  }

  /* check for the maximum number of connections */
  if( instance->open_count >= MAX_CONNECTIONS )
  {
    dev_err(instance->dev, "generic_raw_uart_open(): Too many open connections.");

    /*Release semaphore*/
    up( &instance->sem );

    return -EMFILE;
  }


  if( !instance->open_count )  /*Enable HW for the first connection.*/
  {
    ret = instance->driver->start_connection(&instance->raw_uart);
    if( ret )
    {
      /*Release semaphore*/
      up( &instance->sem );
      return ret;
    }

    instance->rxbuf.head = instance->rxbuf.tail = 0;

    init_waitqueue_head( &instance->writeq );
    init_waitqueue_head( &instance->readq );
  }

  instance->open_count++;

  /*Release semaphore*/
  up( &instance->sem );

  conn = kmalloc( sizeof( struct per_connection_data ), GFP_KERNEL );
  memset( conn, 0, sizeof( struct per_connection_data ) );

  sema_init( &conn->sem, 1 );

  filep->private_data = (void *)conn;

  return 0;
}

static int generic_raw_uart_close(struct inode *inode, struct file *filep)
{
  struct per_connection_data *conn = filep->private_data;
  struct generic_raw_uart_instance *instance = container_of(inode->i_cdev, struct generic_raw_uart_instance, cdev);

  if( down_interruptible(&conn->sem) )
  {
    return -ERESTARTSYS;
  }

  kfree( conn );

  if( down_interruptible(&instance->sem) )
  {
    return -ERESTARTSYS;
  }

  if( instance->open_count )
  {
    instance->open_count--;
  }

  if( !instance->open_count )
  {
    instance->driver->stop_connection(&instance->raw_uart);
  }

  up( &instance->sem );

  return 0;
}

static unsigned int generic_raw_uart_poll(struct file* filep, poll_table* wait)
{
  struct generic_raw_uart_instance *instance = container_of(filep->f_inode->i_cdev, struct generic_raw_uart_instance, cdev);
  struct per_connection_data *conn = filep->private_data;
  unsigned long lock_flags = 0;
  unsigned int mask = 0;

  poll_wait( filep, &instance->readq, wait );
  poll_wait( filep, &instance->writeq, wait );

  spin_lock_irqsave( &instance->lock_tx, lock_flags );
  if( (instance->tx_connection == NULL ) || ( instance->tx_connection->priority < conn->priority ))
  {
    mask |= POLLOUT | POLLWRNORM;
  }
  spin_unlock_irqrestore( &instance->lock_tx, lock_flags );

  if( CIRC_CNT( instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE) > 0 )
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
  int err = 0;
  unsigned long temp;

  if( _IOC_TYPE(cmd) == IOCTL_MAGIC )
  {
    /*
     * extract the type and number bitfields, and don't decode
     * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
     */
    if( _IOC_NR(cmd) > IOCTL_MAXNR )
    {
      return -ENOTTY;
    }

    /*
     * the direction is a bitmask, and VERIFY_WRITE catches R/W
     * transfers. `Type' is user-oriented, while
     * access_ok is kernel-oriented, so the concept of "read" and
     * "write" is reversed
     */
    if( _IOC_DIR(cmd) & _IOC_READ )
    {
      err = !_access_ok( VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd) );
    }
    else if( _IOC_DIR(cmd) & _IOC_WRITE )
    {
      err =  !_access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    }
    if( err )
    {
      return -EFAULT;
    }
  }

  if( down_interruptible(&conn->sem) )
  {
    return -ERESTARTSYS;
  }

  switch( cmd )
  {

  /* Set connection priority */
  case IOCTL_IOCSPRIORITY: /* Set: arg points to the value */
    ret = __get_user( temp,  (unsigned long __user *)arg );
    if( ret )
    {
      break;
    }
    conn->priority = temp;
    break;

    /* Get connection priority */
  case IOCTL_IOCGPRIORITY: /* Get: arg is pointer to result */
    ret = __put_user( conn->priority, (unsigned long __user *)arg );
    break;

    /* Emulated TTY ioctl: Get termios struct */
  case TCGETS:
    if( _access_ok(VERIFY_READ, (void __user *)arg, sizeof(struct termios) ) )
    {
      if( down_interruptible(&instance->sem) )
      {
        ret = -ERESTARTSYS;
      }
      else
      {
        ret = copy_to_user( (void __user *)arg, &instance->termios, sizeof(struct termios) );
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
    if( _access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(struct termios) ) )
    {
      if( down_interruptible(&instance->sem) )
      {
        ret = -ERESTARTSYS;
      }
      else
      {
        ret = copy_from_user( &instance->termios, (void __user *)arg, sizeof(struct termios) );
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
    if( _access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp) ) )
    {
      if( down_interruptible(&instance->sem) )
      {
        ret = -ERESTARTSYS;
      }
      else
      {
        temp = CIRC_CNT( instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE );
        up( &instance->sem );
        ret = __put_user( temp, (int __user *)arg );
      }
    }
    else
    {
      ret = -EFAULT;
    }
    break;

    /* Emulated TTY ioctl: Get send queue size */
  case TIOCOUTQ:
    if( _access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp) ) )
    {
      temp = 0;
      ret = __put_user( temp, (int __user *)arg );
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
    if( _access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp) ) )
    {
      temp = TIOCM_DSR | TIOCM_CD | TIOCM_CTS;
      ret = __put_user( temp, (int __user *)arg );
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

  up( &conn->sem );
  return ret;
}

static int generic_raw_uart_acquire_sender(struct generic_raw_uart_instance *instance, struct per_connection_data *conn )
{
  int ret = 0;
  unsigned long lock_flags;
  int sender_idle;

  spin_lock_irqsave( &instance->lock_tx, lock_flags );
  sender_idle = instance->tx_connection == NULL;
  if( sender_idle || (instance->tx_connection->priority < conn->priority) )
  {
    instance->tx_connection = conn;
    ret = 1;
    if( sender_idle )
    {
      instance->driver->init_tx(&instance->raw_uart);
      generic_raw_uart_tx_queued_unlocked(instance);
    }
    else
    {
      wake_up_interruptible( &instance->writeq );
    }
  }
  spin_unlock_irqrestore(&instance->lock_tx, lock_flags);
  return ret;
}

static int generic_raw_uart_send_completed(struct generic_raw_uart_instance *instance, struct per_connection_data *conn )
{
  int ret = 0;
  unsigned long lock_flags;

  spin_lock_irqsave( &instance->lock_tx, lock_flags );
  ret = instance->tx_connection != conn;
  spin_unlock_irqrestore( &instance->lock_tx, lock_flags );

  return ret;
}

void generic_raw_uart_handle_rx_char(struct generic_raw_uart *raw_uart, enum generic_raw_uart_rx_flags flags, unsigned char data)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;

  instance->count_rx++;

  if(flags & GENERIC_RAW_UART_RX_STATE_BREAK)
  {
    instance->count_brk++;
  }
  else
  {
    if(flags & GENERIC_RAW_UART_RX_STATE_PARITY)
    {
      instance->count_parity++;
    }
    if(flags & GENERIC_RAW_UART_RX_STATE_FRAME)
    {
      instance->count_frame++;
    }
    if(flags & GENERIC_RAW_UART_RX_STATE_OVERRUN)
    {
      instance->count_overrun++;
    }

    if( CIRC_SPACE( instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE ))
    {
      instance->rxbuf.buf[instance->rxbuf.head] = data;
      smp_wmb();

      if( ++(instance->rxbuf.head) >= CIRCBUF_SIZE )
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

void generic_raw_uart_rx_completed(struct generic_raw_uart *raw_uart) {
  struct generic_raw_uart_instance *instance = raw_uart->private;
  wake_up_interruptible( &instance->readq );
}
EXPORT_SYMBOL(generic_raw_uart_rx_completed);

void generic_raw_uart_tx_queued(struct generic_raw_uart *raw_uart)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;

  spin_lock( &instance->lock_tx );
  generic_raw_uart_tx_queued_unlocked(instance);
  spin_unlock( &instance->lock_tx );
}
EXPORT_SYMBOL(generic_raw_uart_tx_queued);

static inline void generic_raw_uart_tx_queued_unlocked(struct generic_raw_uart_instance *instance)
{
  int tx_count = 0;
  int bulksize = 0;

  while( (tx_count < instance->driver->tx_chunk_size) && (instance->driver->isready_for_tx(&instance->raw_uart)) &&
       (instance->tx_connection != NULL) && (instance->tx_connection->tx_buf_index < instance->tx_connection->tx_buf_length) )
  {
    bulksize = min(instance->driver->tx_bulktransfer_size, (int)(instance->tx_connection->tx_buf_length - instance->tx_connection->tx_buf_index));
    instance->driver->tx_chars(&instance->raw_uart, instance->tx_connection->txbuf, instance->tx_connection->tx_buf_index, bulksize);
    instance->tx_connection->tx_buf_index += bulksize;
    smp_wmb();
    tx_count += bulksize;
    #ifdef PROC_DEBUG
    instance->count_tx += bulksize;
    #endif /*PROC_DEBUG*/
  }

  if( (instance->tx_connection != NULL) && (instance->tx_connection->tx_buf_index >= instance->tx_connection->tx_buf_length) )
  {
    instance->driver->stop_tx(&instance->raw_uart);
    instance->tx_connection = NULL;
    smp_wmb();
    wake_up_interruptible( &instance->writeq );
  }
}

#ifdef PROC_DEBUG
static int generic_raw_uart_proc_show(struct seq_file *m, void *v)
{
  struct generic_raw_uart_instance *instance = m->private;

  seq_printf(m, "open_count=%d\n", instance->open_count );
  seq_printf(m, "count_tx=%d\n", instance->count_tx );
  seq_printf(m, "count_rx=%d\n", instance->count_rx );
  seq_printf(m, "count_brk=%d\n", instance->count_brk );
  seq_printf(m, "count_parity=%d\n", instance->count_parity );
  seq_printf(m, "count_frame=%d\n", instance->count_frame );
  seq_printf(m, "count_overrun=%d\n", instance->count_overrun );
  seq_printf(m, "rxbuf_size=%d\n", CIRC_CNT(instance->rxbuf.head, instance->rxbuf.tail, CIRCBUF_SIZE) );
  seq_printf(m, "rxbuf_head=%d\n", instance->rxbuf.head );
  seq_printf(m, "rxbuf_tail=%d\n", instance->rxbuf.tail );

  return 0;
}

static int generic_raw_uart_proc_open(struct inode *inode, struct file *file)
{
  struct generic_raw_uart_instance *instance = PDE_DATA(inode);
  return single_open(file, generic_raw_uart_proc_show, instance);
}
#endif /*PROC_DEBUG*/

const char *generic_raw_uart_get_pin_label(enum generic_raw_uart_pin pin)
{
  switch(pin)
  {
    case GENERIC_RAW_UART_PIN_BLUE:
      return "pivccu,blue_pin";
    case GENERIC_RAW_UART_PIN_GREEN:
      return "pivccu,green_pin";
    case GENERIC_RAW_UART_PIN_RED:
      return "pivccu,red_pin";
    case GENERIC_RAW_UART_PIN_RESET:
      return "pivccu,reset_pin";
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
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0))
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

  char* endp;

  if (simple_strtol(strim((char *)buf), &endp, 0) == 1)
  {
    dev_info(dev, "Reset radio module");
    generic_raw_uart_reset_radio_module(instance);
    return count;
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


static spinlock_t active_devices_lock;
static bool active_devices[MAX_DEVICES] = { false };

struct generic_raw_uart *generic_raw_uart_probe(struct device *dev, struct raw_uart_driver *drv, void *driver_data)
{
  int err;
  int i;

  int dev_no = MAX_DEVICES;

  struct generic_raw_uart_instance *instance;

  unsigned long flags;
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
  if (!instance) {
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
  if (err != 0) {
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

  instance->reset_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, GENERIC_RAW_UART_PIN_RESET);

  if (instance->reset_pin != 0)
  {
    gpio_request(instance->reset_pin, "pivccu:reset");
  }
  else
  {
    dev_info(dev, "No valid reset pin configured in device tree");
  }

  instance->red_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, GENERIC_RAW_UART_PIN_RED);
  instance->green_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, GENERIC_RAW_UART_PIN_GREEN);
  instance->blue_pin = generic_raw_uart_get_gpio_pin_number(instance, dev, GENERIC_RAW_UART_PIN_BLUE);

  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_red_gpio_pin.attr);
  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_green_gpio_pin.attr);
  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_blue_gpio_pin.attr);

  err = sysfs_create_file(&instance->dev->kobj, &dev_attr_reset_radio_module.attr);

  sema_init( &instance->sem, 1 );
  spin_lock_init( &instance->lock_tx );
  init_waitqueue_head( &instance->readq );
  init_waitqueue_head( &instance->writeq );

  instance->rxbuf.buf = kmalloc( CIRCBUF_SIZE, GFP_KERNEL );

#ifdef PROC_DEBUG
  proc_create_data(dev_name(instance->dev), 0444, NULL, &generic_raw_uart_proc_fops, instance);
#endif

  generic_raw_uart_reset_radio_module(instance);

  return &instance->raw_uart;

failed_device_create:
  cdev_del(&instance->cdev);
failed_cdev_add:
  unregister_chrdev_region(instance->devid, 1);
  kfree(instance);
failed_inst_alloc:
  return ERR_PTR(err);
}
EXPORT_SYMBOL(generic_raw_uart_probe);

int generic_raw_uart_remove(struct generic_raw_uart *raw_uart, struct device *dev, struct raw_uart_driver *drv)
{
  struct generic_raw_uart_instance *instance = raw_uart->private;
  unsigned long flags;

#ifdef PROC_DEBUG
  remove_proc_entry(dev_name(instance->dev), NULL);
#endif

  sysfs_remove_file(&instance->dev->kobj, &dev_attr_red_gpio_pin.attr);
  sysfs_remove_file(&instance->dev->kobj, &dev_attr_green_gpio_pin.attr);
  sysfs_remove_file(&instance->dev->kobj, &dev_attr_blue_gpio_pin.attr);

  sysfs_remove_file(&instance->dev->kobj, &dev_attr_reset_radio_module.attr);

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
    int load, ret;

    ret = kstrtoint(val, 10, &load);

    if (load != 1)
    {
        return -EINVAL;
    }

    return request_module("dummy_rx8130");
}

static const struct kernel_param_ops generic_raw_uart_set_dummy_rx8130_loader_param_ops = {
        .set    = generic_raw_uart_set_dummy_rx8130_loader,
};

module_param_cb(load_dummy_rx8130_module, &generic_raw_uart_set_dummy_rx8130_loader_param_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(load_dummy_rx8130_module, "Loads the dummy_rx8130 module");

MODULE_ALIAS("platform:generic-raw-uart");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.11");
MODULE_DESCRIPTION("generic raw uart driver for communication of piVCCU with the HM-MOD-RPI-PCB and RPI-RF-MOD radio modules");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");

