MODULE=driver_SO
 
KERNEL_SRC=/lib/modules/`uname -r`/build
 
obj-m += ${MODULE}.o
 
compile:
	make -C ${KERNEL_SRC} M=${CURDIR} modules

install: 
	sudo insmod ${MODULE}.ko 
	dmesg | tail 
	sudo chmod go+rw /dev/leds
	
uninstall:
	sudo rmmod ${MODULE} 
	dmesg | tail

reinstall:
	sudo rmmod ${MODULE}
	make -C ${KERNEL_SRC} M=${CURDIR} modules
	sudo insmod ${MODULE}.ko 
	dmesg | tail 
 
 
