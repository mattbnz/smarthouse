# Arduino makefile.
#
# Assumes you have the arduino-mk .deb installed.
ARDUINO_DIR = /usr/share/arduino

DIR=$(shell pwd)
TARGET = $(shell basename $(DIR))

BOARDS_TXT  = $(ARDUINO_DIR)/hardware/tiny/boards.txt
PARSE_BOARD = $(ARDUINO_DIR)/ard-parse-boards --boards_txt=$(BOARDS_TXT)

ARDUINO_CORE_PATH = $(ARDUINO_DIR)/hardware/tiny/cores/tiny

JEE_PORT=$(shell ls -1 /dev/ttyUSB? | sort | head -n1)
MEGA_PORT=$(shell ls -1 /dev/ttyACM? | sort | head -n1)
BOARD_TAG = attiny84at8
ARDUINO_PORT = $(MEGA_PORT)

ARDUINO_LIBS = Jeelib

include /usr/share/arduino/Arduino.mk
