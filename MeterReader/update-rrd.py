#!/usr/bin/python
# 
# Copyright (C) 2012 - Matt Brown
#
# All rights reserved.
#
# Example RRD commands for use with this script:
# rrdtool graph /tmp/test.png --start end-48h --lower-limit 0 \
#    --title "Electrical Power Usage" DEF:power=/tmp/power.rrd:node1_kWh:AVERAGE \
#    --vertical-label Watts --width 800 --height 600 \
#    CDEF:watts=power,21600,\* LINE1:watts#ff0000:Power && eog /tmp/test.png
# rrdtool graph /tmp/test.png --start end-48h --lower-limit 0 \
#    --title "Battery voltage" DEF:bat=/tmp/bat.rrd:node1_bat:AVERAGE \
#    --vertical-label mV --width 800 --height 600 CDEF:mv=bat,50,\+,20,\* \
#    LINE1:mv#ff0000:Voltage && eog /tmp/test.png
#
# Reads logger.py output and generates rrd updates.
import os
import rrdtool
import struct
import subprocess
import sys
import time

START_TS = 1351378113
RRA_ALL = 'RRA:AVERAGE:0.9:1:2628000'
RRA_5 = 'RRA:AVERAGE:0.9:5:525600'
POWER_STEP_SIZE = 60

NODE_HANDLERS = {
    1: 'ProcessMeterReader',
    2: 'ProcessTempSensor',
    3: 'ProcessTempSensor',
    4: 'ProcessTempSensor',
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

  def __init__(self, rrd_dir):
    self.rrd_dir = rrd_dir
    self.latest_update = {}
    self.history = {}

  def CheckOrCreateRRDs(self):
    if not os.path.exists(self.RRDForDs('foo_bat')):
      self.CreateRRD('bat')
    if not os.path.exists(self.RRDForDs('foo_temp')):
      self.CreateRRD('temp', 'ProcessTempSensor')
    if not os.path.exists(self.RRDForDs('power')):
      self.CreateRRD('kWh', 'ProcessMeterReader', 'COUNTER:300:U:U')
    
  def CreateRRD(self, suffix, only_handler=None, ds_type='GAUGE:3600:0:255'):
    rrdfile = self.RRDForDs('foo_%s' % suffix)
    data_sources = []
    for node_id, node_handler in NODE_HANDLERS.iteritems():
      if only_handler and node_handler != only_handler:
        continue
      data_sources.append('DS:node%d_%s:%s' % (
        node_id, suffix, ds_type))
    rrdtool.create(rrdfile,
        '--start', str(START_TS), '--step', '300',
        data_sources,
        RRA_ALL, RRA_5)
    print 'Created new RRD %s' % rrdfile

  def ParseLong(self, parts, offset):
    val = 0
    for byte in xrange(0, 4):
      val += int(parts[offset+byte]) << (8*byte)
    return val

  def ParseFloat(self, parts, offset):
    d = ''
    for byte in xrange(0, 4):
      d += chr(int(parts[offset+byte]))
    return struct.unpack('<f', d)

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

  def ParseTempSensorLine(self, parts):
    ts = float(parts[0])
    ping_id = self.ParseLong(parts, 3)
    bat = int(parts[8])
    temp = self.ParseFloat(parts, 9)
    return ts, ping_id, temp, bat

  def RRDForDs(self, ds):
    if ds.endswith('bat'):
      return os.path.join(self.rrd_dir, 'bat.rrd')
    if ds.endswith('temp'):
      return os.path.join(self.rrd_dir, 'temp.rrd')
    return os.path.join(self.rrd_dir, 'power.rrd')

  def UpdateRRD(self, ts, **kwargs):
    files = {}
    for ds, val in kwargs.iteritems():
      rrd = self.RRDForDs(ds)
      files.setdefault(rrd, {})
      files[rrd][ds] = val
    for rrd, data in files.iteritems():
      if ts < self.LastUpdateFor(rrd):
        continue
      keys = data.keys()
      datastr = ':'.join(['%s' % data[k] for k in keys])
      try:
        rrdtool.update(rrd, '-t', ':'.join(keys), '%s:%s' % (int(ts), datastr))
      except rrdtool.error, e:
        print e, 'from', kwargs

  def LastUpdateFor(self, rrd):
    if rrd not in self.latest_update:
      self.latest_update[rrd] = rrdtool.last(rrd)
    return self.latest_update[rrd]

  def GetOrCreateNodeHistory(self, node_id):
    if node_id not in self.history:
      self.history[node_id] = History()
    return self.history[node_id]

  def ProcessFiles(self, files):
    self.CheckOrCreateRRDs()
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

  def IgnoreLine(self, node_id, parts):
    print 'Ignoring line "%s"' % ' '.join(parts)

  def ProcessTempSensor(self, node_id, parts):
    try:
      ts, ping_id, temp, bat = self.ParseTempSensorLine(parts)
    except Exception, e:
      print 'Ignoring line "%s"' % ' '.join(parts), e
      return
    kwargs = {
        'node%d_temp' % node_id: temp,
        'node%d_bat' % node_id: bat,
    }
    self.UpdateRRD(ts, **kwargs)

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
      while last_ts < (ts - (POWER_STEP_SIZE + 1)):
        # Make sure rrd gets data as often as it needs.
        last_ts += POWER_STEP_SIZE
        kwargs = {
            'node%d_kWh' % node_id: hist.realcounter,
            'node%d_bat' % node_id: last_bat,
        }
        self.UpdateRRD(last_ts, **kwargs)

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
    kwargs = {
        'node%d_kWh' % node_id: hist.realcounter,
        'node%d_bat' % node_id: bat,
    }
    self.UpdateRRD(hist.last_ts, **kwargs)
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
  if len(sys.argv) < 2:
    sys.stderr.write('Usage: %s rrd_dir logfile1 [logfile2, ...]\n' %
        sys.argv[0])
    sys.exit(1)
  
  updater = RRDUpdater(sys.argv[1])
  updater.ProcessFiles(sys.argv[2:])
  updater.PrintMeterSummary()

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
