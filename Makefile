obj-m := src/am2302.o

KDIR=/home/yo/self_study/openwrt/build_dir/target-mipsel_24kc_musl/linux-ramips_mt76x8/linux-5.15.150
CROSS_COMPILE=/home/yo/self_study/openwrt/staging_dir/toolchain-mipsel_24kc_gcc-12.3.0_musl/bin/mipsel-openwrt-linux-musl-
export STAGING_DIR=/home/yo/self_study/openwrt/staging_dir

all:
	make -C ${KDIR} ARCH=mips CROSS_COMPILE=${CROSS_COMPILE} M=$(PWD) modules

clean:
	make -C ${KDIR} M=$(PWD) clean