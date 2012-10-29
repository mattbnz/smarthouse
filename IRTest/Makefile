# Arduino makefile for MotorDriver.
#
# Assumes you have the arduino-mk .deb installed.
ARDUINO_DIR = /usr/share/arduino

TARGET = IRTest
ARDUINO_LIBS = 

BOARDS_TXT  = $(ARDUINO_DIR)/hardware/arduino/boards.txt
PARSE_BOARD = $(ARDUINO_DIR)/ard-parse-boards --boards_txt=$(BOARDS_TXT)

include /usr/share/arduino/Arduino.mk

jee_upload:
	BOARD_TAG=uno ARDUINO_PORT=/dev/ttyUSB0 $(MAKE) upload

jee_console: jee_upload
	screen /dev/ttyUSB0 57600

mega_upload:
	BOARD_TAG=mega2560 ARDUINO_PORT=/dev/ttyACM0 $(MAKE) upload

mega_console: mega_upload
	screen /dev/ttyACM0 57600
