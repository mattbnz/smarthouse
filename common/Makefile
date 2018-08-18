OPENWRT_TOOLCHAIN = /home/matt/openwrt/OpenWrt-Toolchain-ar71xx-for-mips_r2-gcc-4.3.3+cs_uClibc-0.9.30.1/toolchain-mips_r2_gcc-4.3.3+cs_uClibc-0.9.30.1/usr/bin

jeelogger.mips: jeelogger.c
	CC=$(OPENWRT_TOOLCHAIN)/mips-openwrt-linux-uclibc-gcc LD=$(OPENWRT_TOOLCHAIN)/mips-openwrt-linux-uclibc-ld $(OPENWRT_TOOLCHAIN)/mips-openwrt-linux-uclibc-gcc jeelogger.c -o jeelogger.mips -static
