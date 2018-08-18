# Arduino makefile.
#
# Assumes you have the arduino-mk .deb installed.
ARDUINO_DIR   = /usr/share/arduino
ARDMK_DIR     = /usr/share/arduino

DIR=$(shell pwd)
TARGET = $(shell basename $(DIR))

#BOARDS_TXT  = $(ARDUINO_DIR)/hardware/arduino/boards.txt
#PARSE_BOARD = $(ARDUINO_DIR)/ard-parse-boards --boards_txt=$(BOARDS_TXT)

include /usr/share/arduino/Arduino.mk

JEE_PORT=$(shell ls -1 /dev/ttyUSB? | sort | head -n1)
MEGA_PORT=$(shell ls -1 /dev/ttyACM? | sort | head -n1)

jee_upload:
	BOARD_TAG=uno ARDUINO_PORT=$(JEE_PORT) $(MAKE) upload

jee_console: jee_upload
	screen $(JEE_PORT) 57600

mega_upload:
	BOARD_TAG=mega2560 ARDUINO_PORT=$(MEGA_PORT) $(MAKE) upload

mega_console: mega_upload
	screen $(MEGA_PORT) 57600
