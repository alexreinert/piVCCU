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

#include "stack_protector.include"

static int red_gpio_pin = 0;
static int green_gpio_pin = 0;
static int blue_gpio_pin = 0;

static struct rpi_rf_mod_led_led *red = 0;
static struct rpi_rf_mod_led_led *green = 0;
static struct rpi_rf_mod_led_led *blue = 0;

struct rpi_rf_mod_led_led
{
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

static struct rpi_rf_mod_led_led *rpi_rf_mod_led_createled(const char *name, bool initial)
{
  struct rpi_rf_mod_led_led *led;

  led = kzalloc(sizeof(struct rpi_rf_mod_led_led), GFP_KERNEL);

  led->cdev.name = name;
  led->cdev.brightness_set = rpi_rf_mod_led_set_led_brightness;
  led->cdev.brightness_get = rpi_rf_mod_led_get_led_brightness;
  led->cdev.default_trigger = initial ? "default-on" : "none";
  led->gpio = 0;
  led->brightness = initial ? LED_FULL : LED_OFF;

  led_classdev_register(NULL, &led->cdev);

  return led;
}

static void rpi_rf_mod_led_set_gpio_pin(struct rpi_rf_mod_led_led *led, int gpio)
{
  if (led == 0)
    return;

  if (led->gpio != 0)
  {
    gpio_free(led->gpio);
  }

  if (gpio != 0 && gpio_is_valid(gpio))
  {
    gpio_request(gpio, led->cdev.name);
    gpio_direction_output(gpio, false);
    gpio_set_value(gpio, led->brightness == LED_OFF ? 0 : 1);
  }
  else
  {
    gpio = 0;
  }

  led->gpio = gpio;
}

static void rpi_rf_mod_led_destroyled(struct rpi_rf_mod_led_led *led)
{
  led_classdev_unregister(&led->cdev);

  if (led->gpio != 0)
  {
    gpio_free(led->gpio);
  }

  kfree(led);
}

static int __init rpi_rf_mod_led_init(void)
{
  red = rpi_rf_mod_led_createled("rpi_rf_mod:red", true);
  green = rpi_rf_mod_led_createled("rpi_rf_mod:green", true);
  blue = rpi_rf_mod_led_createled("rpi_rf_mod:blue", false);

  rpi_rf_mod_led_set_gpio_pin(red, red_gpio_pin);
  rpi_rf_mod_led_set_gpio_pin(green, green_gpio_pin);
  rpi_rf_mod_led_set_gpio_pin(blue, blue_gpio_pin);

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

static int rpi_rf_mod_led_set_param(const char *val, const struct kernel_param *kp)
{
  int gpio, ret;
  struct rpi_rf_mod_led_led *led;

  ret = kstrtoint(val, 10, &gpio);

  if (ret != 0)
    return -EINVAL;

  if (strcmp(kp->name, "red_gpio_pin") == 0)
  {
    red_gpio_pin = gpio;
    led = red;
  }
  else if (strcmp(kp->name, "green_gpio_pin") == 0)
  {
    green_gpio_pin = gpio;
    led = green;
  }
  else if (strcmp(kp->name, "blue_gpio_pin") == 0)
  {
    blue_gpio_pin = gpio;
    led = blue;
  }
  else
  {
    return -ENODEV;
  }

  rpi_rf_mod_led_set_gpio_pin(led, gpio);

  return 0;
}

static int rpi_rf_mod_led_get_param(char *buffer, const struct kernel_param *kp)
{
  int value = 0;

  if (strcmp(kp->name, "red_gpio_pin") == 0)
  {
    value = red->gpio;
  }
  else if (strcmp(kp->name, "green_gpio_pin") == 0)
  {
    value = green->gpio;
  }
  else if (strcmp(kp->name, "blue_gpio_pin") == 0)
  {
    value = blue->gpio;
  }
  else
  {
    return -ENODEV;
  }

  return sprintf(buffer, "%d", value);
}

static const struct kernel_param_ops rpi_rf_mod_led_gpio_param_ops = {
    .set = rpi_rf_mod_led_set_param,
    .get = rpi_rf_mod_led_get_param,
};

module_param_cb(red_gpio_pin, &rpi_rf_mod_led_gpio_param_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(red_gpio_pin, "GPIO Pin of red LED");

module_param_cb(green_gpio_pin, &rpi_rf_mod_led_gpio_param_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(green_gpio_pin, "GPIO Pin of green LED");

module_param_cb(blue_gpio_pin, &rpi_rf_mod_led_gpio_param_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(blue_gpio_pin, "GPIO Pin of blue LED");

MODULE_AUTHOR("Alexander Reinert <alex@areinert.de>");
MODULE_DESCRIPTION("GPIO LED driver for RPI-RF-MOD");
MODULE_VERSION("1.7");
MODULE_LICENSE("GPL");
MODULE_ALIAS("rpi_rf_mod_led");
