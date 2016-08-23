#!/usr/bin/python
# 
# Copyright (C) 2014 - Matt Brown
#
# All rights reserved.
#
# Requires pySerial (apt-get install python-serial).
#
# Logs lines received on the serial port from the Arduino controlling the water
# level sensor.
import calendar
import fcntl
import os
import select
import serial
import sys
import syslog
import time

  
class Logfile(object):
  logdir = None
  fd = None
  expires_at = None


def CreateLogfile(log, now):
  now = time.gmtime()
  path = os.path.join(log.logdir, time.strftime('%Y%m%d%H.log', now))
  syslog.syslog('Creating new logfile for %d at %s' % (calendar.timegm(now), path))
  log.fd = open(path, 'a')
  log.expires_at = calendar.timegm(now) + (59 - now.tm_sec) + (60 * (59 - now.tm_min))
  return True

def CheckLogfile(log, now):
  if log.expires_at == -1:
    return CreateLogfile(log, now)
  elif now < log.expires_at:
    return True
  # Rotation needed
  log.fd.close()
  return CreateLogfile(log, now)


def main():
  if len(sys.argv) != 3:
    sys.stderr.write('Usage: %s /path/to/serial/port /path/to/logdir\n' % sys.argv[0])
    sys.exit(1)

  syslog.openlog('tanklogger', syslog.LOG_CONS | syslog.LOG_PID, syslog.LOG_DAEMON)

  log = Logfile()
  log.logdir = sys.argv[2]
  log.expires_at = -1
  log.fd = -1

  ser = serial.Serial(sys.argv[1], 57600, timeout=0)
  time.sleep(2)
  syslog.syslog('Entering main read loop')
  buf = ''
  while 1:
    new = ser.readline()
    buf += new
    now = time.time()
    nl = buf.find('\n')
    if nl != -1:
      if not CheckLogfile(log, now):
        return 3
      log.fd.write('%d %s\n' % (now,  buf[:nl]))
      log.fd.flush()
      buf = buf[nl+1:]

  syslog.syslog('Exiting')

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
