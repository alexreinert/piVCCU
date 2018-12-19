/*-----------------------------------------------------------------------------
 * Copyright (c) 2018 by Alexander Reinert
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
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>

static int red_gpio_pin = 0;
module_param(red_gpio_pin, int, S_IRUSR | S_IRGRP);
MODULE_PARM_DESC(red_gpio_pin, "GPIO Pin of red LED");

static int green_gpio_pin = 0;
module_param(green_gpio_pin, int, S_IRUSR | S_IRGRP);
MODULE_PARM_DESC(green_gpio_pin, "GPIO Pin of green LED");

static int blue_gpio_pin = 0;
module_param(blue_gpio_pin, int, S_IRUSR | S_IRGRP);
MODULE_PARM_DESC(blue_gpio_pin, "GPIO Pin of blue LED");

static struct rpi_rf_mod_led_led *red;
static struct rpi_rf_mod_led_led *green;
static struct rpi_rf_mod_led_led *blue;

struct rpi_rf_mod_led_led {
  struct led_classdev cdev;
  int gpio;
  enum led_brightness brightness;
};

static void rpi_rf_mod_led_set_led_brightness(struct led_classdev *cdev, enum led_brightness b)
{
  struct rpi_rf_mod_led_led *led = container_of(cdev, struct rpi_rf_mod_led_led, cdev);

  led->brightness = b;

  if (led->gpio != 0)
  {
    gpio_set_value(led->gpio, b == LED_OFF ? 0 : 1);
  }
}

static enum led_brightness rpi_rf_mod_led_get_led_brightness(struct led_classdev *cdev)
{
  struct rpi_rf_mod_led_led *led = container_of(cdev, struct rpi_rf_mod_led_led, cdev);
  return led->brightness;
}

static struct rpi_rf_mod_led_led* rpi_rf_mod_led_createled(const char* name, int gpio, bool initial)
{
    struct rpi_rf_mod_led_led* led;

    led = kzalloc(sizeof(struct rpi_rf_mod_led_led), GFP_KERNEL);

    if (gpio != 0 && gpio_is_valid(gpio))
    {
      gpio_request(gpio, name);
      gpio_direction_output(gpio, false);
      gpio_set_value(gpio, 0);
    }
    else
    {
      gpio = 0;
    }

    led->cdev.name = name;
    led->cdev.brightness_set = rpi_rf_mod_led_set_led_brightness;
    led->cdev.brightness_get = rpi_rf_mod_led_get_led_brightness;
    led->cdev.default_trigger = initial ? "default-on" : "none";
    led->gpio = gpio;
    led->brightness = LED_OFF;

    led_classdev_register(NULL, &led->cdev);

    return led;
}

static void rpi_rf_mod_led_destroyled(struct rpi_rf_mod_led_led* led)
{
  if (led->gpio != 0)
  {
    gpio_free(led->gpio);
  }

  led_classdev_unregister(&led->cdev);

  kfree(led);
}

static int __init rpi_rf_mod_led_init(void)
{
  red = rpi_rf_mod_led_createled("rpi_rf_mod:red", red_gpio_pin, true);
  green = rpi_rf_mod_led_createled("rpi_rf_mod:green", green_gpio_pin, true);
  blue = rpi_rf_mod_led_createled("rpi_rf_mod:blue", blue_gpio_pin, false);

  return 0;
}

static void __exit rpi_rf_mod_led_exit(void)
{
  rpi_rf_mod_led_destroyled(red);
  rpi_rf_mod_led_destroyled(green);
  rpi_rf_mod_led_destroyled(blue);
}

module_init(rpi_rf_mod_led_init);
module_exit(rpi_rf_mod_led_exit);

MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
MODULE_DESCRIPTION("GPIO LED driver for RPI-RF-MOD");
MODULE_VERSION("1.2");
MODULE_LICENSE("GPL");
MODULE_ALIAS("rpi_rf_mod_led");

