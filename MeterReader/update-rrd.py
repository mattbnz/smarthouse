#!/usr/bin/python
# vim: set fileencoding=utf8
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
import optparse
import os
import rrdtool
import struct
import subprocess
import sys
import time

START_TS = 1351378113
RRA_LAST = 'RRA:LAST:0.9:1:2628000'
RRA_5 = 'RRA:AVERAGE:0.9:5:525600'
RRA_24H_MIN = 'RRA:MIN:0.9:288:1825'
RRA_24H_MAX = 'RRA:MAX:0.9:288:1825'
RRA_24H_AVG = 'RRA:AVERAGE:0.9:288:1825'
RRAS = (RRA_LAST, RRA_5, RRA_24H_MIN, RRA_24H_MAX, RRA_24H_AVG)
POWER_STEP_SIZE = 60

NODE_HANDLERS = {
    1: 'ProcessMeterReader',
    2: 'ProcessTempSensor',
    3: 'ProcessTempSensor',
    4: 'ProcessTempSensor',
}

def ParseLong(parts, offset):
  val = 0
  for byte in xrange(0, 4):
    val += int(parts[offset+byte]) << (8*byte)
  return val

def ParseFloat(parts, offset):
  d = ''
  for byte in xrange(0, 4):
    d += chr(int(parts[offset+byte]))
  return struct.unpack('<f', d)[0]

def FormatHour(hour):
  return '%s-%s-%s %s:00' % (hour[:4], hour[4:6], hour[6:8], hour[8:])


class History(object):

  def __init__(self):
    # Common attributes.
    self.last_ts = 0
    self.last_ping_id = 0
    self.num_reports = 0
    self.received_reports = 0
    self.gaps = []
    # Meter Reader attributes.
    self.first_count = 0
    self.first_ts = 0
    self.hour_counter = 0
    self.realcount = 0
    self.lastline = None
    # Temp Sensor attributes.
    self.temps = []


class Report(object):

  def __init__(self, line, debug):
    self.valid = False
    parts = line.strip().split(' ')
    if len(parts) < 8:
      if debug:
        print 'Skipping bad line: %s' % line.strip()
      return
    if parts[1] != 'OK':
      if debug:
        print 'Skipping invalid line: %s' % line.strip()
      return
    try:
      self.ts = float(parts[0])
      self.node_id = int(parts[2])
      self.ping_id = ParseLong(parts, 3)
    except ValueError, e:
      if debug:
        print 'Skipping invalid line: %s' % line.strip(), e
      return
    self.parts = parts[8:]
    t = time.gmtime(self.ts)
    self.hour = '%04d%02d%02d%02d' % (t.tm_year, t.tm_mon, t.tm_mday,
        t.tm_hour)
    self.valid = True

  def __str__(self):
    return '%d@%d (%d): %s' % (self.node_id, self.ts, self.ping_id,
        ' '.join(self.parts))

class RRDUpdater(object):

  def __init__(self, rrd_dir, update_rrds, debug=False):
    self.rrd_dir = rrd_dir
    self.rrds = []
    self.update_rrds = update_rrds
    self.debug = debug
    self.update_ts = None
    self.update_queue = {}
    self.latest_update = {}
    self.history = {}
    self.current_line = None
    self.current_hour = None

  def CheckOrCreateRRD(self, ds):
    rrd = self.RRDForDs(ds)
    if rrd in self.rrds:
      return
    if not os.path.exists(self.RRDForDs(ds)):
      self.CreateRRD(ds)
    else:
      self.rrds.append(rrd)
    
  def CreateRRD(self, ds):
    if ds.endswith('bat') or ds.endswith('temp'):
      ds_type = 'GAUGE:3600:0:255'
    else:
      ds_type = 'COUNTER:300:U:U'
    rrdfile = self.RRDForDs(ds)
    if self.update_rrds:
      try:
        rrdtool.create(rrdfile,
            '--start', str(START_TS), '--step', '300',
            ['DS:%s:%s' % (ds, ds_type)],
            *RRAS)
      except rrdtool.error, e:
        sys.stderr.write('ERROR: Could not create rrd %s for %s: %s\n' %
            (rrdfile, ds, e))
        sys.exit(1)
    self.rrds.append(rrdfile)
    print 'Created new RRD %s' % rrdfile

  def ParseMeterLine(self, parts):
    bat = int(parts[0])
    if len(parts) == 2:
      # Old format, single byte counter.
      counter = int(parts[1])
    elif len(parts) >= 5:
      # New format, long counter.
      counter = ParseLong(parts, 1)
    return counter, bat

  def ParseTempSensorLine(self, parts):
    bat = int(parts[0])
    temp = ParseFloat(parts, 1)
    return temp, bat

  def RRDForDs(self, ds):
    return os.path.join(self.rrd_dir, '%s.rrd' % ds)

  def UpdateRRD(self, ts, updates):
    if self.update_ts and self.update_ts != ts:
      self.FlushUpdateQueue()
    # Queue requested updates for insertion.
    self.update_queue.update(updates)
    self.update_ts = ts

  def FlushUpdateQueue(self):
    files = {}
    for ds, val in self.update_queue.iteritems():
      self.CheckOrCreateRRD(ds)
      rrd = self.RRDForDs(ds)
      files.setdefault(rrd, {})
      files[rrd][ds] = val
    for rrd, data in files.iteritems():
      if self.update_ts < self.LastUpdateFor(rrd):
        if self.debug:
          print 'ignoring update for %s, too old' % rrd, data
        continue
      keys = data.keys()
      datastr = ':'.join(['%s' % data[k] for k in keys])
      try:
        if self.update_rrds:
          rrdtool.update(rrd, '-t', ':'.join(keys),
              '%s:%s' % (int(self.update_ts), datastr))
        elif self.debug:
          print ('rrdtool update -t', ':'.join(keys),
              '%s:%s' % (int(self.update_ts), datastr))
      except rrdtool.error, e:
        print e, 'from', self.update_queue, 'at', self.current_line
    self.update_queue = {}

  def LastUpdateFor(self, rrd):
    if not self.update_rrds and not os.path.exists(rrd):
      return 0
    if rrd not in self.latest_update:
      self.latest_update[rrd] = rrdtool.last(rrd)
    return self.latest_update[rrd]

  def GetOrCreateNodeHistory(self, node_id):
    if node_id not in self.history:
      self.history[node_id] = History()
    return self.history[node_id]

  def UpdateNodeReport(self, report):
    hist = self.GetOrCreateNodeHistory(report.node_id)
    if hist.last_ts > 0:
      if (int(hist.last_ts/3600)*3600) == (int(report.ts/3600)*3600):
        hist.gaps.append(report.ts - hist.last_ts)
      if report.ping_id > (hist.last_ping_id + 1):
        hist.num_reports += report.ping_id - hist.last_ping_id
    hist.num_reports += 1
    hist.received_reports += 1
    # Store state for future.
    hist.last_ping_id = report.ping_id
    hist.last_ts = report.ts

  def CalcHourlyAverageAndReset(self, node_id, hist, just):
    a = '% 2d: ' % node_id
    if NODE_HANDLERS[node_id] == 'ProcessMeterReader':
      usage = hist.realcounter - hist.hour_counter
      hist.hour_counter = hist.realcounter
      a += '%.02fkWh' % (usage*6/1000.0)
    elif NODE_HANDLERS[node_id] == 'ProcessTempSensor':
      if len(hist.temps) > 0:
        a += '%.02f°C' % (sum(hist.temps) / len(hist.temps))
        just += 1  # degree confuses ljust... sigh.
      hist.temps = []
    hist.num_reports = 0
    hist.received_reports = 0
    hist.gaps = []
    return a.ljust(just)

  def PrintHourlyReport(self):
    reliability = []
    averages = []
    for node_id in sorted(NODE_HANDLERS.keys()):
      hist = self.GetOrCreateNodeHistory(node_id)
      health = freq = '   NaN'
      if hist.num_reports > 0:
        health = '% 5d%%' % int(
            float(hist.received_reports) / float(hist.num_reports) * 100.0)
        if len(hist.gaps) > 0:
          freq = '% 5ds' % (sum(hist.gaps) / float(len(hist.gaps)))
      debug = ''
      if self.debug:
        debug = ' (% 3d/% 3d)' % (hist.received_reports, hist.num_reports)
      t = '% 2d:%s @%s%s' % (node_id, health, freq, debug)
      reliability.append(t)
      averages.append(self.CalcHourlyAverageAndReset(node_id, hist, len(t)))
    hour = FormatHour(self.current_hour)
    print '%s: Reports : %s' % (hour, ' '.join(reliability))
    print '%s: Averages: %s' % (hour, ' '.join(averages))

  def ProcessFiles(self, files):
    for filename in files:
      for line in open(filename, 'r'):
        self.current_line = line
        report = Report(line, self.debug)
        if not report.valid:
          continue
        if self.current_hour and report.hour != self.current_hour:
          self.PrintHourlyReport()
        self.current_hour = report.hour
        # Handle the line depending on the node type.
        handler = getattr(self,
            NODE_HANDLERS.get(report.node_id, 'IgnoreLine'))
        handler(report)
        # Keep stats about node report reliability every hour.
        self.UpdateNodeReport(report)
    # Make sure the last report gets flushed.
    self.FlushUpdateQueue()

  def IgnoreLine(self, report):
    if self.debug:
      print 'Ignoring report ', report

  def ProcessTempSensor(self, report):
    try:
      temp, bat = self.ParseTempSensorLine(report.parts)
      if temp > 40.0:
        raise RuntimeError('Temp too high to be believable!')
    except Exception, e:
      print 'Ignoring bad temp report ', report, e
      return
    hist = self.GetOrCreateNodeHistory(report.node_id)
    hist.temps.append(temp)
    data = {
        'node%d_temp' % report.node_id: temp,
        'node%d_bat' % report.node_id: bat,
    }
    self.UpdateRRD(report.ts, data)

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
      if len_parts < 5:
        if last_counter > ((2*8)*0.9):
          return 6

      # Handle wrap with new-style long counter.
      if last_counter > ((2**32)*0.9):
        return 2**32 - last_counter

      # No wrap, just reset.
      return 0
    
    # Simple case last. Just trust the report.
    return counter - last_counter

  def ProcessMeterReader(self, report):
    try:
      counter, bat = self.ParseMeterLine(report.parts)
    except Exception, e:
      print 'Ignoring bad meter report ', report, e
      return
    hist = self.GetOrCreateNodeHistory(report.node_id)
    if hist.lastline:
      last_counter, last_bat = self.ParseMeterLine(hist.lastline)
      # I think this causes more harm than good now, since battery/temp
      # measurements from other sensors can move the rrd ts past what we try to
      # synthesize. Meter Reader should be reporting frequently enough for this
      # not to be required anymore. Can deal with crappy gaps from the first
      # few days when this was useful.
      #while last_ts < (ts - (POWER_STEP_SIZE + 1)):
      #  # Make sure rrd gets data as often as it needs.
      #  last_ts += POWER_STEP_SIZE
      #  data = {
      #      'node%d_revs' % node_id: hist.realcounter,
      #      'node%d_bat' % node_id: last_bat,
      #  }
      #  self.UpdateRRD(last_ts, data)

      hist.realcounter += self.CalculateStep(report.ping_id, counter, last_counter,
          hist.last_ping_id, len(report.parts))
    else:
      hist.realcounter = counter
      hist.first_count = counter
      hist.first_ts = report.ts
      hist.hour_counter = counter
    hist.lastline = report.parts
    data = {
        'node%d_revs' % report.node_id: hist.realcounter,
        'node%d_bat' % report.node_id: bat,
    }
    self.UpdateRRD(report.ts, data)

  def PrintMeterSummary(self):
    hist = self.GetOrCreateNodeHistory(1)
    usage = hist.realcounter - hist.first_count
    print 'Kwh from %s til %s: %.02fkWh' % (
      time.ctime(hist.first_ts), time.ctime(hist.last_ts), usage*6/1000.0)

def main():
  parser = optparse.OptionParser()
  parser.add_option('--dry_run', action='store_true', dest='dry_run')
  parser.add_option('--debug', action='store_true', dest='debug')
  options, args = parser.parse_args()
  if len(args) < 2:
    sys.stderr.write('Usage: %s [--dry_run] [--debug] rrd_dir '
    'logfile1 [logfile2, ...]\n' % sys.argv[0])
    sys.exit(1)

  updater = RRDUpdater(args[0], not options.dry_run, options.debug)
  updater.ProcessFiles(args[1:])
  updater.PrintMeterSummary()

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
