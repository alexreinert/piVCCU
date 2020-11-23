#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "stack_protector.include"

static int __init dummy_rx8130_init(void)
{
  return 0;
}

static void __exit dummy_rx8130_exit(void)
{
}

module_init(dummy_rx8130_init);
module_exit(dummy_rx8130_exit);

MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
MODULE_DESCRIPTION("Dummy Module to trick the hss_led");
MODULE_VERSION("1.1");
MODULE_LICENSE("GPL");
MODULE_ALIAS("dummy_rx8130");
