obj-m += src/driver/am2302_platform_char_driver.o

all:
	make -C ${KDIR} ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} M=$(PWD) modules
	dtc -@ -O dtb -o src/dts/am2302_platform.dtbo src/dts/am2302_platform.dts

clean:
	make -C ${KDIR} M=$(PWD) clean
