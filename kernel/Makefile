obj-m += eq3_char_loop.o
obj-m += plat_eq3ccu2.o
obj-m += generic_raw_uart.o
obj-m += pl011_raw_uart.o
obj-m += dw_apb_raw_uart.o

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

