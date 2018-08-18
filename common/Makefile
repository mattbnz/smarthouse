OPENWRT_STAGING = /home/matt/openwrt/staging_dir
OPENWRT_TOOLCHAIN = $(OPENWRT_STAGING)/toolchain-mips_34kc_gcc-5.3.0_musl-1.1.15/bin/

jeelogger.mips: jeelogger.c
	CC=$(OPENWRT_TOOLCHAIN)/mips-openwrt-linux-gcc LD=$(OPENWRT_TOOLCHAIN)/mips-openwrt-linux-ld $(OPENWRT_TOOLCHAIN)/mips-openwrt-linux-gcc jeelogger.c -o jeelogger.mips -static
