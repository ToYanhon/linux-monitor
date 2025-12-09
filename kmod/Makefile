obj-m := cpu_load_monitor_kmod.o cpu_softirq_monitor_kmod.o cpu_stat_monitor_kmod.o

KDIR := /lib/modules/$(shell uname -r)/build
CC := /usr/bin/gcc-12

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules CC=$(CC)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install