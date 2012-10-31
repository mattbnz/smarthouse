#!/usr/bin/python
# 
# Copyright (C) 2012 - Matt Brown
#
# All rights reserved.
#
# Example RRD commands for use with this script:
# rrdtool create /tmp/test.rrd -b 1351378113 -s 60 \
#    DS:kWh:COUNTER:300:U:U DS:bat:GAUGE:300:U:U \
#    RRA:AVERAGE:0.5:1:2628000 RRA:AVERAGE:0.5:5:525600
# rrdtool graph /tmp/test.png --start end-48h --lower-limit 0 \
#    --title "Electrical Power Usage" DEF:power=/tmp/test.rrd:kWh:AVERAGE \
#    --vertical-label Watts --width 800 --height 600 \
#    CDEF:watts=power,21600,\* LINE1:watts#ff0000:Power && eog /tmp/test.png
# rrdtool graph /tmp/test.png --start end-48h --lower-limit 0 \
#    --title "Battery voltage" DEF:bat=/tmp/test.rrd:bat:AVERAGE \
#    --vertical-label mV --width 800 --height 600 CDEF:mv=bat,50,\+,20,\* \
#    LINE1:mv#ff0000:Voltage && eog /tmp/test.png
#
# Reads logger.py output and generates rrd updates.
import os
import sys
import time

STEP_SIZE = 300

def parse_long(parts, offset):
  val = 0
  for byte in xrange(0, 4):
    val += int(parts[offset+byte]) << (8*byte)
  return val

def parse_line(line):
  parts = line.strip().split(' ')
  ts = float(parts[0])
  ping_id = parse_long(parts, 3)
  bat = int(parts[8])
  if len(parts) == 10:
    # Old format, single byte counter.
    counter = int(parts[9])
  elif len(parts) == 13:
    # New format, long counter.
    counter = parse_long(parts, 9)
  return ts, ping_id, counter, bat


def update_rrd(ts, counter, bat):
  cmd = 'rrdtool update %s %s:%s:%s' % (sys.argv[2], int(ts), counter, bat)
  #print time.ctime(ts), cmd
  os.system(cmd)

def main():
  if len(sys.argv) < 2:
    sys.stderr.write('Usage: %s /path/to/log /path/to/rrd\n' % sys.argv[0])
    sys.exit(1)
  
  first_count = 0
  first_ts = 0
  start_ts = 0
  start_counter = 0
  realcount = 0
  lastline = None
  for line in open(sys.argv[1], 'r'):
    if ' OK ' not in line:
      continue
    parts = line.strip().split(' ')
    ts, ping_id, counter, bat = parse_line(line)
    if lastline:
      last_ts, last_ping, last_counter, last_bat = parse_line(lastline)
      while last_ts < (ts - (STEP_SIZE + 1)):
        # Make sure rrd gets data as often as it needs.
        last_ts += STEP_SIZE
        update_rrd(last_ts, realcounter, last_bat)

      if ping_id == 1:
        # Reboot
        step = 1
      elif ping_id - 1 != last_ping:
        if counter > last_counter:
          # Easy, just subtract.
          step = counter - last_counter
        else:
          # Harder, use ping_id to judge missing reports.
          missing = ping_id - last_ping
          if len(parts) < 13:
            step = (10 * missing) - 4   # 10 counts per report, minus 4 for wrap.
          else:
            # Unlikely!
            step = missing
      else:
        if counter == 0:
          if len(parts) < 13:
            # Handle wrap with old-style byte counter.
            step = 6
          else:
            # Handle wrap with new-style long counter.
            step = 2**32 - last_counter
        else:
          step = counter - last_counter
      realcounter += step
    else:
      realcounter = first_count = counter
      start_ts = first_ts = ts
      start_counter = counter
    lastline = line
    update_rrd(ts, realcounter, bat)
    if ts - start_ts > 3600:
      usage = realcounter - start_counter
      print 'Kwh from %s til %s: %.02fkWh' % (time.ctime(start_ts), time.ctime(ts), usage*6/1000.0)
      start_ts = ts
      start_counter = realcounter
  usage = realcounter - first_count
  print 'Kwh from %s til %s: %.02fkWh' % (time.ctime(first_ts), time.ctime(ts), usage*6/1000.0)

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
