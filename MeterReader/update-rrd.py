#!/usr/bin/python
# 
# Copyright (C) 2012 - Matt Brown
#
# All rights reserved.
#
# Example RRD commands for use with this script:
# rrdtool create /tmp/test.rrd -b 1351378113 -s 300 \
#    DS:kWh:COUNTER:300:U:U RRA:AVERAGE:0.5:1:2628000 RRA:AVERAGE:0.5:5:525600
# rrdtool graph /tmp/test.png --start end-48h --lower-limit 0 \
#    --title "Electrical Power Usage" DEF:power=/tmp/test.rrd:kWh:AVERAGE \
#    --vertical-label Watts --width 800 --height 600 \
#    CDEF:watts=power,21600,\* LINE1:watts#ff0000:Power && eog /tmp/test.png
#
# Reads logger.py output and generates rrd updates.
import os
import sys
import time

STEP_SIZE = 300

def get_ping_id(parts):
  ping_id = 0
  for byte in xrange(0, 4):
    ping_id += int(parts[3+byte]) << (8*byte)
  return ping_id

def update_rrd(ts, counter):
  cmd = 'rrdtool update %s %s:%s' % (sys.argv[2], int(ts), counter)
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
    ts = float(parts[0])
    ping_id = get_ping_id(parts)
    counter = int(parts[9])
    if lastline:
      last_ts = float(lastline[0])
      while last_ts < (ts - (STEP_SIZE + 1)):
        # Make sure rrd gets data as often as it needs.
        last_ts += STEP_SIZE
        update_rrd(last_ts, realcounter)

      last_ping = get_ping_id(lastline)
      last_counter = int(lastline[9])
      if ping_id - 1 != last_ping:
        if counter > last_counter:
          # Easy, just subtract.
          step = counter - last_counter
        else:
          # Harder, use ping_id to judge missing reports.
          missing = ping_id - last_ping
          step = (10 * missing) - 4   # 10 counts per report, minus 4 for wrap.
      else:
        if counter == 0:
          # Handle wrap.
          step = 6
        else:
          step = counter - last_counter
          assert step == 10, 'step %s on %s vs %s' % (step, parts, lastline)
      realcounter += step
    else:
      realcounter = first_count = counter
      start_ts = first_ts = ts
      start_counter = counter
    lastline = parts
    update_rrd(ts, realcounter)
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
