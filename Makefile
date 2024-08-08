obj-m += src/am2302_char_driver.o
obj-m += src/am2302_platform_char_driver.o

all:
	make -C ${KDIR} ARCH=mips CROSS_COMPILE=${CROSS_COMPILE} M=$(PWD) modules
	dtc -@ -O dtb -o src/am2302_platform.dtbo src/am2302_platform.dts

clean:
	make -C ${KDIR} M=$(PWD) clean