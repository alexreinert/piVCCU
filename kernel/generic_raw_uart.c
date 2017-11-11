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
#include <linux/delay.h>

#include "generic_raw_uart.h"

#define MODULE_NAME "generic-raw-uart"
#define DRIVER_NAME "raw-uart"

#define CIRCBUF_SIZE 1024
#define CON_DATA_TX_BUF_SIZE 4096
#define PROC_DEBUG  1
#define IOCTL_MAGIC 'u'
#define IOCTL_MAXNR 2
#define MAX_CONNECTIONS 3
#define IOCTL_IOCSPRIORITY _IOW(IOCTL_MAGIC,  1, unsigned long) /* Set the priority for the current channel */
#define IOCTL_IOCGPRIORITY _IOR(IOCTL_MAGIC,  2, unsigned long) /* Get the priority for the current channel */

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
  int gpio_pin;

  int count_tx;                               /*Statistic counter: Number of bytes transmitted*/
  int count_rx;                               /*Statistic counter: Number of bytes received*/
  int count_brk;                              /*Statistic counter: Number of break conditions received*/
  int count_parity;                           /*Statistic counter: Number of parity errors*/
  int count_frame;                            /*Statistic counter: Number of frame errors*/
  int count_overrun;                          /*Statistic counter: Number of RX overruns in hardware FIFO*/
  int count_buf_overrun;                      /*Statistic counter: Number of RX overruns in user space buffer*/
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
static int generic_raw_uart_acquire_sender( struct per_connection_data *conn );
static int generic_raw_uart_send_completed( struct per_connection_data *conn );
static void generic_raw_uart_tx_queued_unlocked(void);
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

static struct generic_raw_uart_instance *instance;
static dev_t generic_raw_uart_devid;
static struct cdev generic_raw_uart_cdev;
static struct class *generic_raw_uart_class;
static struct device *generic_raw_uart_dev;

const struct raw_uart_driver *driver;

static ssize_t generic_raw_uart_read(struct file *filep, char __user *buf, size_t count, loff_t *offset)
{
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
  struct per_connection_data *conn = filep->private_data;
  int ret = 0;

  if( down_interruptible(&conn->sem) )
  {
    ret = -ERESTARTSYS;
    goto exit;
  }

  if( count > sizeof(conn->txbuf)  )
  {
    dev_err(generic_raw_uart_dev, "generic_raw_uart_write(): Error message size.");
    ret = -EMSGSIZE;
    goto exit_sem;
  }

  if( copy_from_user(conn->txbuf, buf, count) )
  {
    dev_err(generic_raw_uart_dev, "generic_raw_uart_write(): Copy from user.");
    ret = -EFAULT;
    goto exit_sem;
  }

  conn->tx_buf_index = 0;
  conn->tx_buf_length = count;
  smp_wmb();  /*Wait until completion of all writes*/

  if( wait_event_interruptible(instance->writeq, generic_raw_uart_acquire_sender(conn)) )
  {
    ret = -ERESTARTSYS;
    goto exit_sem;
  }

  /*wait for sending to complete*/
  if( wait_event_interruptible(instance->writeq, generic_raw_uart_send_completed(conn)) )
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

static void generic_raw_uart_reset_radio_module(void)
{
  if (instance->gpio_pin != 0)
  {
    gpio_set_value(instance->gpio_pin, 0);
    msleep(100);
    gpio_set_value(instance->gpio_pin, 1);
  }
}

static int generic_raw_uart_open(struct inode *inode, struct file *filep)
{
  int ret;
  struct per_connection_data *conn;

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
    dev_err(generic_raw_uart_dev, "generic_raw_uart_open(): Too many open connections.");

    /*Release semaphore*/
    up( &instance->sem );

    return -EMFILE;
  }


  if( !instance->open_count )  /*Enable HW for the first connection.*/
  {
    ret = driver->start_connection();
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
    driver->stop_connection();
    generic_raw_uart_reset_radio_module();
  }

  up( &instance->sem );

  return 0;
}

static unsigned int generic_raw_uart_poll(struct file* filep, poll_table* wait)
{
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
      err = !access_ok( VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd) );
    }
    else if( _IOC_DIR(cmd) & _IOC_WRITE )
    {
      err =  !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
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
    if( access_ok(VERIFY_READ, (void __user *)arg, sizeof(struct termios) ) )
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
    if( access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(struct termios) ) )
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
    if( access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp) ) )
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
    if( access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp) ) )
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
    if( access_ok(VERIFY_WRITE, (void __user *)arg, sizeof(temp) ) )
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

static int generic_raw_uart_acquire_sender( struct per_connection_data *conn )
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
      driver->init_tx();
      generic_raw_uart_tx_queued_unlocked();
    }
    else
    {
      wake_up_interruptible( &instance->writeq );
    }
  }
  spin_unlock_irqrestore(&instance->lock_tx, lock_flags);
  return ret;
}

static int generic_raw_uart_send_completed( struct per_connection_data *conn )
{
  int ret = 0;
  unsigned long lock_flags;

  spin_lock_irqsave( &instance->lock_tx, lock_flags );
  ret = instance->tx_connection != conn;
  spin_unlock_irqrestore( &instance->lock_tx, lock_flags );

  return ret;
}

void generic_raw_uart_handle_rx_char(enum generic_raw_uart_rx_flags flags, unsigned char data)
{
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
      dev_err(generic_raw_uart_dev, "generic_raw_uart_handle_rx_char(): rx fifo full.");
    }
  }
}
EXPORT_SYMBOL(generic_raw_uart_handle_rx_char);

void generic_raw_uart_rx_completed(void) {
  wake_up_interruptible( &instance->readq );
}
EXPORT_SYMBOL(generic_raw_uart_rx_completed);

void generic_raw_uart_tx_queued(void)
{
  spin_lock( &instance->lock_tx );
  generic_raw_uart_tx_queued_unlocked();
  spin_unlock( &instance->lock_tx );
}
EXPORT_SYMBOL(generic_raw_uart_tx_queued);

static inline void generic_raw_uart_tx_queued_unlocked(void)
{
  int tx_count = 0;

  while( (tx_count < driver->tx_chunk_size) && (driver->isready_for_tx()) &&
       (instance->tx_connection != NULL) && (instance->tx_connection->tx_buf_index < instance->tx_connection->tx_buf_length) )
  {
    driver->tx_char(instance->tx_connection->txbuf[instance->tx_connection->tx_buf_index]);
    instance->tx_connection->tx_buf_index++;
    smp_wmb();
    tx_count++;
    #ifdef PROC_DEBUG
    instance->count_tx++;
    #endif /*PROC_DEBUG*/
  }

  if( (instance->tx_connection != NULL) && (instance->tx_connection->tx_buf_index >= instance->tx_connection->tx_buf_length) )
  {
    driver->stop_tx( );
    instance->tx_connection = NULL;
    smp_wmb();
    wake_up_interruptible( &instance->writeq );
  }
}

#ifdef PROC_DEBUG
static int generic_raw_uart_proc_show(struct seq_file *m, void *v)
{
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

static int generic_raw_uart_proc_open(struct inode *inode, struct  file *file)
{
  return single_open( file, generic_raw_uart_proc_show, NULL );
}
#endif /*PROC_DEBUG*/

int generic_raw_uart_probe(struct device *dev, struct raw_uart_driver *drv)
{
  int err;
  int val;
  void *ptr_err;

  driver = drv;

  instance = kzalloc(sizeof(struct generic_raw_uart_instance), GFP_KERNEL);
  if (!instance) {
    err = -ENOMEM;
    goto failed_inst_alloc;
  }

  /* create char device */
  err = alloc_chrdev_region(&generic_raw_uart_devid, 0, 1, DRIVER_NAME);
  if (err != 0) {
    dev_err(dev, "unable to allocate device number");
    goto failed_alloc_chrdev;
  }
  cdev_init(&generic_raw_uart_cdev, &generic_raw_uart_fops);
  err = cdev_add(&generic_raw_uart_cdev, generic_raw_uart_devid, 1);
  if (err != 0) {
    dev_err(dev, "unable to register device");
    goto failed_cdev_add;
  }

  /* create sysfs entries */
  generic_raw_uart_class = class_create(THIS_MODULE, DRIVER_NAME);
  ptr_err = generic_raw_uart_class;
  if (IS_ERR(ptr_err))
    goto failed_class_create;

  generic_raw_uart_dev = device_create(generic_raw_uart_class,
                                       NULL,
                                       generic_raw_uart_devid,
                                       NULL,
                                       DRIVER_NAME);
  ptr_err = generic_raw_uart_dev;
  if (IS_ERR(ptr_err))
    goto failed_device_create;

  err = device_property_read_u32(dev, "pivccu,gpio_pin", &val);
  if (!err)
    instance->gpio_pin = val;

  if (instance->gpio_pin != 0 && gpio_is_valid(instance->gpio_pin))
  {
    gpio_request(instance->gpio_pin, NULL);
    gpio_direction_output(instance->gpio_pin, true);
  }
  else
  {
    dev_info(dev, "No reset pin configured in device tree");
    instance->gpio_pin = 0;
  }

  sema_init( &instance->sem, 1 );
  spin_lock_init( &instance->lock_tx );
  init_waitqueue_head( &instance->readq );
  init_waitqueue_head( &instance->writeq );

  instance->rxbuf.buf = kmalloc( CIRCBUF_SIZE, GFP_KERNEL );

#ifdef PROC_DEBUG
  proc_create(DRIVER_NAME, 0444, NULL, &generic_raw_uart_proc_fops);
#endif

  generic_raw_uart_reset_radio_module();

  return 0;

failed_device_create:
  class_destroy(generic_raw_uart_class);
failed_class_create:
  cdev_del(&generic_raw_uart_cdev);
  err = PTR_ERR(ptr_err);
failed_cdev_add:
  unregister_chrdev_region(generic_raw_uart_devid, 1);
failed_alloc_chrdev:
  kfree(instance);
failed_inst_alloc:
  return err;
}
EXPORT_SYMBOL(generic_raw_uart_probe);

int generic_raw_uart_remove(struct device *dev, struct raw_uart_driver *drv)
{
#ifdef PROC_DEBUG
  remove_proc_entry(DRIVER_NAME, NULL);
#endif

  if (instance->gpio_pin != 0)
  {
    gpio_free(instance->gpio_pin);
  }

  kfree(instance);
  device_destroy(generic_raw_uart_class, generic_raw_uart_devid);
  class_destroy(generic_raw_uart_class);
  cdev_del(&generic_raw_uart_cdev);
  unregister_chrdev_region(generic_raw_uart_devid, 1);

  return 0;
}
EXPORT_SYMBOL(generic_raw_uart_remove);

static int __init generic_raw_uart_init(void)
{
  return 0;
}

static void __exit generic_raw_uart_exit(void)
{
  return;
}

module_init(generic_raw_uart_init);
module_exit(generic_raw_uart_exit);

MODULE_ALIAS("platform:generic-raw-uart");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("generic raw uart driver for communication of piVCCU with the HM-MOD-RPI-PCB module");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");

