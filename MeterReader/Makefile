# Assumes you have the arduino-mk .deb installed.
ARDUINO_LIBS = Jeelib OneWire DallasTemp

include ../smarthouse.mk

jeelogger.mips: jeelogger.c
	CC=mips-openwrt-linux-uclibc-gcc LD=mips-openwrt-linux-uclibc-ld mips-openwrt-linux-uclibc-gcc jeelogger.c -o jeelogger.mips -static
