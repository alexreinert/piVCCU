obj-m += bcm2835_raw_uart.o
obj-m += eq3_char_loop.o
obj-m += plat_eq3ccu2.o

modules:
	make -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	make -C $(KERNEL_DIR) M=$(PWD) clean
