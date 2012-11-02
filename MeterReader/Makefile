# Assumes you have the arduino-mk .deb installed.
ARDUINO_DIR = /usr/share/arduino

TARGET = MeterReader
ARDUINO_LIBS = Jeelib

BOARDS_TXT  = $(ARDUINO_DIR)/hardware/arduino/boards.txt
PARSE_BOARD = $(ARDUINO_DIR)/ard-parse-boards --boards_txt=$(BOARDS_TXT)

include /usr/share/arduino/Arduino.mk

# Mips toolchain downloaded from http://downloads.openwrt.org/backfire/10.03.1/ar71xx/OpenWrt-Toolchain-ar71xx-for-mips_r2-gcc-4.3.3%2bcs_uClibc-0.9.30.1.tar.bz2
MIPS_TOOLCHAIN = /usr/local/OpenWrt-Toolchain-ar71xx-for-mips_r2-gcc-4.3.3+cs_uClibc-0.9.30.1/toolchain-mips_r2_gcc-4.3.3+cs_uClibc-0.9.30.1
GCC_MIPS = $(MIPS_TOOLCHAIN)/usr/bin/mips-openwrt-linux-gcc

clean:
	rm -rf build-cli jeelogger jeelogger.mips

jee_upload: clean
	BOARD_TAG=uno ARDUINO_PORT=/dev/ttyUSB0 $(MAKE) upload

jee_console: jee_upload
	screen /dev/ttyUSB0 57600

mega_upload: clean
	BOARD_TAG=mega2560 ARDUINO_PORT=/dev/ttyACM0 $(MAKE) upload

mega_console: mega_upload
	screen /dev/ttyACM0 57600

jeelogger: jeelogger.c
	gcc -o jeelogger jeelogger.c

jeelogger.mips: jeelogger.c
	$(GCC_MIPS) -static -o jeelogger.mips jeelogger.c
