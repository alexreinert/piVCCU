obj-m += eq3_char_loop.o
obj-m += plat_eq3ccu2.o
obj-m += generic_raw_uart.o
obj-m += pl011_raw_uart.o
obj-m += dw_apb_raw_uart.o
obj-m += meson_raw_uart.o
obj-m += fake_hmrf.o
obj-m += rpi_rf_mod_led.o
obj-m += dummy_rx8130.o
obj-m += led_trigger_timer.o
obj-m += hb_rf_usb.o
obj-m += rtc-rx8130.o

ifeq ($(KERNELRELEASE),)
  KERNELRELEASE := $(shell uname -r)
endif

ifeq ($(KERNEL_DIR),)
  KERNEL_DIR := /lib/modules/$(KERNELRELEASE)/build
endif

all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

