#include <linux/kernel.h> /*needed for priority messages in prink*/
#include <linux/init.h> /*needed for macros*/
#include <linux/module.h> /*needed for all modules*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexander Reinert");
MODULE_DESCRIPTION("plat_eq3ccu2 CCU2 emulation module");

static char *board_serial = "";
static char *radio_mac = "";
static short eq3charloop_major = 0;
static short uart_major = 0;

module_param(board_serial, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(board_serial, "Board serial number, e.g. MEQ1234567");
module_param(radio_mac, charp, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(radio_mac, "Radio mac, e.g. 0x123456");

module_param(eq3charloop_major, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(eq3charloop_major, "Device major number of eq3_char_loop");
module_param(uart_major, short, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
MODULE_PARM_DESC(uart_major, "Device major number of raw uart");

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

