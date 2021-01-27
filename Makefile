ifneq ($(KERNELRELEASE),)
	obj-m := tablet_driver.o
else
	CURRENT = $(shell uname -r)
	KDIR = /lib/modules/$(CURRENT)/build
	PWD = $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	rm -rf .tmp_versions
	rm *.o
	rm *.mod.c
	rm *.symvers
	rm *.order
	rm .*.cmd
endif