#!/usr/bin/python
# 
# Copyright (C) 2012 - Matt Brown
#
# All rights reserved.
#
# Requires pySerial (apt-get install python-serial).
#
# Logs packets written to the serial port by the Jeelink receiving packets from
# the Jeenode running MeterReader.ino. Assumes the Jeelink is running something
# like the RF12demo sketch from Jeelib.
import fcntl
import os
import select
import serial
import sys
import time
  
def main():
  if len(sys.argv) != 2:
    sys.stderr.write('Usage: %s /path/to/serial/port\n' % sys.argv[0])
    sys.exit(1)

  ser = serial.Serial(sys.argv[1], 57600, timeout=0)
  time.sleep(2)
  print 'Ready for action!'
  ser.write('h\n')
  ser.write('1 q\n')  # Set quiet mode (ignore bad packets)
  buf = ''
  while 1:
    new = ser.readline()
    buf += new
    nl = buf.find('\n')
    if nl != -1:
      print time.time(), buf[:nl]
      buf = buf[nl+1:]

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
