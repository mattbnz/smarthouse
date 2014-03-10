#!/usr/bin/python
# vim: set fileencoding=utf8
# 
# Copyright (C) 2014 - Matt Brown
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
import cPickle as pickle
import math
import optparse
import os
import rrdtool
import struct
import subprocess
import sys
import time

# Based on Cloverly, Water Tank under lawn.
# 
TANK_DEPTH_CM = 248.5  # From bottom of sensor
TANK_RADIUS_CM = 150
  
START_TS = 1394270170
# Assuming 60s step size.
RRA_LAST = 'RRA:LAST:0.9:1:2628000'    # 5 years of exact measurements.
RRA_5 = 'RRA:AVERAGE:0.9:5:1051200'    # 10 years of 5min averages.
RRA_60 = 'RRA:AVERAGE:0.9:60:87600'    # 10 years of 1hr averages.
RRAS = (RRA_LAST, RRA_5, RRA_60)

NODE_HANDLERS = {
    100: 'ProcessTankLevel',
}


def FormatHour(hour):
  return '%s-%s-%s %s:00' % (hour[:4], hour[4:6], hour[6:8], hour[8:])


class NodeState(object):
  """Stores the current state and statistics for an individual node.""" 

  def __init__(self):
    # Common attributes.
    self.last_ts = 0
    self.num_reports = 0
    self.received_reports = 0
    self.gaps = []
    # Water sensor attribute
    self.last_litres = 0
    self.hour_litres = 0	

class Report(object):

  def __init__(self, line, debug):
    self.valid = False
    parts = line.strip().split(' ')
    if len(parts) < 2:
      if debug:
        print 'Skipping bad line: %s' % line.strip()
      return
    try:
      self.ts = float(parts[0])
    except ValueError, e:
      if debug:
        print 'Skipping invalid line: %s' % line.strip(), e
      return
    self.node_id = 100  # HARDCODED FOR NOW!!
    self.parts = parts[1:]
    t = time.gmtime(self.ts)
    self.hour = '%04d%02d%02d%02d' % (t.tm_year, t.tm_mon, t.tm_mday,
        t.tm_hour)
    self.valid = True

  def __str__(self):
    return '%d: %s' % (self.ts, ' '.join(self.parts))


class RRDUpdaterHistory(object):
  """Stores the history for what has been processed to date."""

  def __init__(self):
    self.latest_update = {}
    self.node_state = {}
    self.current_hour = None
    self.current_file = None
    self.current_file_lineno = None


class RRDUpdater(object):
  """Updates RRDs based on a directory of logfiles."""

  def __init__(self, rrd_dir, history_file, dry_run, debug=False):
    # self.history must be defined first to avoid infinite loop in setattr.
    self.rrd_dir = rrd_dir
    self.rrds = []
    self.dry_run = dry_run
    self.debug = debug
    self.current_line = None
    self.update_ts = None
    self.update_queue = {}
    self.history_file = history_file
    if history_file and os.path.exists(history_file):
      self.history = pickle.load(file(history_file, 'rb'))
      print 'Loaded history from %s. Current Hour: %s. Processing %s@%s' % (
          history_file, self.current_hour, self.current_file,
          self.current_file_lineno)
    else:
      self.history = RRDUpdaterHistory()

  def __getattr__(self, name):
    """Delegate to the history object for any attributes it defines."""
    history = self.__dict__.get('history', None)
    if not hasattr(history, name):
      raise AttributeError
    return getattr(history, name)

  def __setattr__(self, name, value):
    """Save to the history object for any attributes it defines."""
    history = self.__dict__.get('history', None)
    if hasattr(history, name):
      setattr(history, name, value)
    else:
      self.__dict__[name] = value

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
      ds_type = 'GAUGE:3600:-50:255'
    elif ds.endswith('litres'):
      ds_type = 'GAUGE:3600:0:20000'
    elif ds.endswith('change'):
      ds_type = 'ABSOLUTE:60:U:U'
    else:
      ds_type = 'COUNTER:300:U:U'
    rrdfile = self.RRDForDs(ds)
    if not self.dry_run:
      try:
        rrdtool.create(rrdfile,
            '--start', str(START_TS), '--step', '60',
            ['DS:%s:%s' % (ds, ds_type)],
            *RRAS)
      except rrdtool.error, e:
        sys.stderr.write('ERROR: Could not create rrd %s for %s (%s): %s\n' %
            (rrdfile, ds, ds_type, e))
        sys.exit(1)
    self.rrds.append(rrdfile)
    print 'Created new RRD %s' % rrdfile

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
      last_update = self.LastUpdateFor(rrd)
      if self.update_ts < last_update:
        if self.debug:
          print 'ignoring update for %s, too old (%d < %d)' % (
	    rrd, self.update_ts, last_update), data
        continue
      keys = data.keys()
      datastr = ':'.join(['%s' % data[k] for k in keys])
      try:
        if not self.dry_run:
          rrdtool.update(rrd, '-t', ':'.join(keys),
              '%s:%s' % (int(self.update_ts), datastr))
        elif self.debug:
          print ('rrdtool update -t', ':'.join(keys),
              '%s:%s' % (int(self.update_ts), datastr))
      except rrdtool.error, e:
        print e, 'from', self.update_queue, 'at', self.current_line
    self.update_queue = {}

  def SaveHistory(self):
    if self.dry_run or not self.history_file:
      return True
    fp = open('%s.tmp' % self.history_file, 'wb')
    pickle.dump(self.history, fp, pickle.HIGHEST_PROTOCOL)
    fp.close()
    os.rename('%s.tmp' % self.history_file, self.history_file)
    print 'History saved to %s' % self.history_file

  def LastUpdateFor(self, rrd):
    if self.dry_run and not os.path.exists(rrd):
      return 0
    if rrd not in self.latest_update:
      self.latest_update[rrd] = rrdtool.last(rrd)
    return self.latest_update[rrd]

  def GetOrCreateNodeState(self, node_id):
    if node_id not in self.node_state:
      self.node_state[node_id] = NodeState()
    return self.node_state[node_id]

  def UpdateNodeReport(self, report):
    state = self.GetOrCreateNodeState(report.node_id)
    if state.last_ts > 0:
      if (int(state.last_ts/3600)*3600) == (int(report.ts/3600)*3600):
        state.gaps.append(report.ts - state.last_ts)
    state.num_reports += 1
    state.received_reports += 1
    # Store state for future.
    state.last_ts = report.ts

  def CalcHourlyAverageAndReset(self, node_id, state, just):
    a = '% 2d: ' % node_id
    if NODE_HANDLERS[node_id] == 'ProcessMeterReader':
      usage = state.realcounter - state.hour_counter
      state.hour_counter = state.realcounter
      a += '%.02fkWh' % (usage*6/1000.0)
    elif NODE_HANDLERS[node_id] == 'ProcessTempSensor':
      if len(state.temps) > 0:
        a += '%.02f°C' % (sum(state.temps) / len(state.temps))
        just += 1  # degree confuses ljust... sigh.
      state.temps = []
    elif NODE_HANDLERS[node_id] == 'ProcessTankLevel':
      change = state.last_litres - state.hour_litres
      state.hour_litres = state.last_litres
      a += '%.02fL (%s %.02fL)' % (state.last_litres,
                                   change >= 0 and '+' or '-', abs(change))
    state.num_reports = 0
    state.received_reports = 0
    state.gaps = []
    return a.ljust(just)

  def PrintHourlyReport(self):
    reliability = []
    averages = []
    for node_id in sorted(NODE_HANDLERS.keys()):
      state = self.GetOrCreateNodeState(node_id)
      health = freq = '   NaN'
      if state.num_reports > 0:
        health = '% 5d%%' % int(
            float(state.received_reports) / float(state.num_reports) * 100.0)
        if len(state.gaps) > 0:
          freq = '% 5ds' % (sum(state.gaps) / float(len(state.gaps)))
      debug = ''
      if self.debug:
        debug = ' (% 3d/% 3d)' % (state.received_reports, state.num_reports)
      t = '% 2d:%s @%s%s' % (node_id, health, freq, debug)
      reliability.append(t)
      averages.append(self.CalcHourlyAverageAndReset(node_id, state, len(t)))
    hour = FormatHour(self.current_hour)
    print '%s: Reports : %s' % (hour, ' '.join(reliability))
    print '%s: Averages: %s' % (hour, ' '.join(averages))

  def ProcessFiles(self, files):
    hist_file = self.current_file
    hist_lineno = self.current_file_lineno
    last_ts = 0
    for filename in files:
      basename = os.path.basename(filename)
      if hist_file and basename < hist_file:
        #print 'Skipping %s, already processed' % basename
        continue
      self.current_file = basename
      for lineno, line in enumerate(open(filename, 'r')):
        if hist_file == self.current_file:
          if lineno <= hist_lineno:
            #print 'Skipping line %d in %s, already processed' % (lineno, basename)
            continue
        self.current_file_lineno = lineno
        self.current_line = line
        report = Report(line, self.debug)
        if not report.valid:
          continue
        if last_ts == 0:
          last_ts = report.ts
        else:
          elapsed = report.ts - last_ts
          if elapsed > 0 and elapsed < 58:
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
    # Print an update.
    self.PrintHourlyReport()
    # Save history
    self.SaveHistory()

  def IgnoreLine(self, report):
    if self.debug:
      print 'Ignoring report ', report

  def ParseTankLevelLine(self, parts):
    if parts[0].startswith('Distance'):
     return float(parts[1])
    return float(parts[0])
       
  def ProcessTankLevel(self, report):
    try:
      level = self.ParseTankLevelLine(report.parts)
    except Exception, e:
      print 'Ignoring bad report ', report, e
      return
    # There was a bug in our sensor data between 1394365998 and 1394417865 due
    # to loading code onto the Arduino that was compiled on the cloverly
    # server, rather than Andrew's laptop. For some reason the same code
    # compiled on these different machines results in an 11cm measurement
    # difference! Andrew's laptop is correct, the server is not.
    if report.ts >= 1394365998 and report.ts <= 1394417865:
      level -= 11
    # Convert the level to litres
    water_level = TANK_DEPTH_CM - level
    litres = (math.pi * (TANK_RADIUS_CM * TANK_RADIUS_CM) * water_level) / 1000.0
    state = self.GetOrCreateNodeState(report.node_id)
    usage = 0
    if state.last_litres != 0:
      change = litres - state.last_litres
      state.last_litres = litres
    else:
      state.last_litres = litres
      state.hour_litres = litres
      change = 0
    data = {
        'tank_litres': litres,
        'tank_change': change
    }
    self.UpdateRRD(report.ts, data)


def main():
  parser = optparse.OptionParser()
  parser.add_option('--dry_run', action='store_true', dest='dry_run')
  parser.add_option('--debug', action='store_true', dest='debug')
  parser.add_option('--history_file', action='store', dest='history_file')
  options, args = parser.parse_args()
  if len(args) < 2:
    sys.stderr.write('Usage: %s [--dry_run] [--debug] [--history_file foo] '
        'rrd_dir logfile1 [logfile2, ...]\n' % sys.argv[0])
    sys.exit(1)

  updater = RRDUpdater(args[0], options.history_file, options.dry_run,
      options.debug)
  updater.ProcessFiles(args[1:])

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
