FILE_NAME := ./keyboard_tablet.ko
MODULES := keyboard_tablet wacom hid_generic usbhid hid

ifneq ($(KERNELRELEASE),)
	obj-m := keyboard_tablet.o
else
	CURRENT = $(shell uname -r)
	KDIR = /lib/modules/$(CURRENT)/build
	PWD = $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	@rm -f *.o .*.cmd .*.flags *.mod.c *.order
	@rm -f .*.*.cmd *~ *.*~ TODO.*
	@rm -fR .tmp*
	@rm -rf .tmp_versions

disclean: clean
	@rm *.ko *.symvers

remove_modules:
	sudo rmmod $(MODULES)

insert_module:
	sudo insmod $(FILE_NAME)

reload_module: all
	make remove_modules || make insert_module

endif
