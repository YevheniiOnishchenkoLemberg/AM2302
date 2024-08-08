obj-$(CONFIG_CHAR_DRIVER) += src/am2302_char_driver.o
obj-$(CONFIG_CHAR_PLATFORM_DRIVER) += src/am2302_platform_char_driver.o

all:
	bash -c "source environment.env"
	make -C ${KDIR} ARCH=mips CROSS_COMPILE=${CROSS_COMPILE} M=$(PWD) modules
ifeq ($(CONFIG_CHAR_PLATFORM_DRIVER), m)
	dtc -@ -O dtb -o src/am2302_platform.dtbo src/am2302_platform.dts
endif

clean:
	make -C ${KDIR} M=$(PWD) clean
