/*-----------------------------------------------------------------------------
 * Copyright (c) 2020 by Alexander Reinert
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

#include <linux/kernel.h> /*needed for priority messages in prink*/
#include <linux/init.h>   /*needed for macros*/
#include <linux/module.h> /*needed for all modules*/

#include "stack_protector.include"

static char *board_serial = "";
static char *radio_mac = "";
static char *board_extended_info = "";
static short eq3charloop_major = 0;
static short uart_major = 0;
static short hmip_major = 0;
static short hmip_minor = 0;

module_param(board_serial, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(board_serial, "Board serial number, e.g. MEQ1234567");
module_param(radio_mac, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(radio_mac, "Radio mac, e.g. 0x123456");

module_param(board_extended_info, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(board_extended_info, "Extended board informations, e.g. firmware version");

module_param(eq3charloop_major, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(eq3charloop_major, "Device major number of eq3_char_loop");
module_param(uart_major, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(uart_major, "Device major number of raw uart");

module_param(hmip_major, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(hmip_major, "Device major number of HmIP device");
module_param(hmip_minor, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(hmip_minor, "Device minor number of HmIP device");

static int __init plat_eq3ccu2_init(void)
{
  printk(KERN_INFO "Started plat_eq3ccu2\n");
  return 0;
}

static void __exit plat_eq3ccu2_exit(void)
{
  printk(KERN_INFO "Stopped plat_eq3ccu2\n");
}

module_init(plat_eq3ccu2_init);
module_exit(plat_eq3ccu2_exit);

MODULE_LICENSE("GPL");
MODULE_VERSION("1.4");
MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
MODULE_DESCRIPTION("plat_eq3ccu2 CCU2 emulation module");
