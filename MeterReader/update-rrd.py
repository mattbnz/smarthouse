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
import subprocess
import sys
import time

STEP_SIZE = 60
latest_update = -1

NODE_HANDLERS = {
    1: 'ProcessMeterReader',
    2: 'ProcessTempSensor',
    3: 'ProcessTempSensor',
}

class History(object):

  def __init__(self):
    self.first_count = 0
    self.first_ts = 0
    self.last_ts = 0
    self.start_ts = 0
    self.start_counter = 0
    self.realcount = 0
    self.lastline = None

class RRDUpdater(object):

  def __init__(self, rrdfile):
    self.rrdfile = rrdfile
    self.latest_update = self.GetLastRRDUpdate()
    self.history = {}

  def ParseLong(self, parts, offset):
    val = 0
    for byte in xrange(0, 4):
      val += int(parts[offset+byte]) << (8*byte)
    return val

  def ParseMeterLine(self, parts):
    ts = float(parts[0])
    ping_id = self.ParseLong(parts, 3)
    bat = int(parts[8])
    if len(parts) == 10:
      # Old format, single byte counter.
      counter = int(parts[9])
    elif len(parts) >= 13:
      # New format, long counter.
      counter = self.ParseLong(parts, 9)
    return ts, ping_id, counter, bat

  def UpdateRRD(self, ts, **kwargs):
    if ts < self.latest_update:
      return
    keys = kwargs.keys()
    template = '-t %s' % ':'.join(keys)
    datastr = ':'.join(['%s' % kwargs[k] for k in keys])
    cmd = 'rrdtool update %s %s %s:%s' % (self.rrdfile, template, int(ts), datastr)
    #print time.ctime(ts), cmd
    os.system(cmd)

  def GetLastRRDUpdate(self):
    cmd = ['rrdtool', 'info', self.rrdfile]
    output = subprocess.Popen(cmd, stdout=subprocess.PIPE).communicate()[0]
    for line in output.split('\n'):
      if not line.startswith('last_update'):
        continue
      return int(line.strip()[14:])
    return -1

  def GetOrCreateNodeHistory(self, node_id):
    if node_id not in self.history:
      self.history[node_id] = History()
    return self.history[node_id]

  def ProcessFiles(self, files):
    for filename in files:
      for line in open(filename, 'r'):
        parts = line.strip().split(' ')
        if len(parts) < 3:
          continue
        if parts[1] != 'OK':
          continue
        node_id = int(parts[2])
        handler = getattr(self, NODE_HANDLERS.get(node_id, 'IgnoreLine'))
        handler(node_id, parts)

  def IgnoreLine(self, parts):
    pass

  def ProcessTempSensor(self, node_id, parts):
    pass

  def CalculateStep(self, ping_id, counter, last_counter, last_ping, len_parts):
    if ping_id == 1 or counter < last_counter:
      # Reboot
      return 1

    if ping_id - 1 != last_ping:
      if counter >= last_counter:
        # Easy, just subtract.
        return counter - last_counter
      
      # Harder, use ping_id to judge missing reports.
      missing = ping_id - last_ping
      if len_parts < 13:
        return (10 * missing) - 4   # 10 counts per report, minus 4 for wrap.

      # Unlikely!
      return missing
    
    # Might be a wrap... Check if last counter value was high.
    if counter == 0:
      # Handle wrap with old-style byte counter.
      if len_parts < 13:
        if last_counter > ((2*8)*0.9):
          return 6

      # Handle wrap with new-style long counter.
      if last_counter > ((2**32)*0.9):
        return 2**32 - last_counter

      # No wrap, just reset.
      return 0
    
    # Simple case last. Just trust the report.
    return counter - last_counter

  def ProcessMeterReader(self, node_id, parts):
    try:
      ts, ping_id, counter, bat = self.ParseMeterLine(parts)
    except:
      return
    hist = self.GetOrCreateNodeHistory(node_id)
    if hist.lastline:
      last_ts, last_ping, last_counter, last_bat = self.ParseMeterLine(hist.lastline)
      while last_ts < (ts - (STEP_SIZE + 1)):
        # Make sure rrd gets data as often as it needs.
        last_ts += STEP_SIZE
        self.UpdateRRD(last_ts, kWh=hist.realcounter, bat=last_bat)

      hist.realcounter += self.CalculateStep(ping_id, counter, last_counter,
          last_ping, len(parts))
    else:
      hist.realcounter = counter
      hist.first_count = counter
      hist.start_ts = ts
      hist.first_ts = ts
      hist.start_counter = counter
    hist.lastline = parts
    hist.last_ts = ts
    self.UpdateRRD(ts, kWh=hist.realcounter, bat=bat)
    if ts - hist.start_ts > 3600:
      usage = hist.realcounter - hist.start_counter
      print 'Kwh from %s til %s: %.02fkWh' % (
        time.ctime(hist.start_ts), time.ctime(ts), usage*6/1000.0)
      hist.start_ts = ts
      hist.start_counter = hist.realcounter

  def PrintMeterSummary(self):
    hist = self.GetOrCreateNodeHistory(1)
    usage = hist.realcounter - hist.first_count
    print 'Kwh from %s til %s: %.02fkWh' % (
      time.ctime(hist.first_ts), time.ctime(hist.last_ts), usage*6/1000.0)

def main():
  global latest_update
  if len(sys.argv) < 2:
    sys.stderr.write('Usage: %s rrd_file logfile1 [logfile2, ...]\n' %
        sys.argv[0])
    sys.exit(1)
  
  updater = RRDUpdater(sys.argv[1])
  updater.ProcessFiles(sys.argv[2:])
  updater.PrintMeterSummary()

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
