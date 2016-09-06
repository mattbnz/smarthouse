#!/usr/bin/python
# vim: set fileencoding=utf8
# 
# Copyright (C) 2016 - Matt Brown
#
# All rights reserved.
#
# Reads logger.py output and pushes to SD
import cPickle as pickle
from gcloud import monitoring
import optparse
import os
import rrdtool
import struct
import subprocess
import sys
import time

NODE_HANDLERS = {
    2: 'ProcessTempSensor',
    3: 'ProcessTempSensor',
    4: 'ProcessTempSensor',
    5: 'ProcessTempSensor',
}
TEMPERATURE = 'custom.googleapis.com/smarthouse/temperature'
BATTERY = 'custom.googleapis.com/smarthouse/battery'


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
  if hour:
    return '%s-%s-%s %s:00' % (hour[:4], hour[4:6], hour[6:8], hour[8:])
  else:
    return ''


class NodeState(object):
  """Stores the current state and statistics for an individual node.""" 

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
    self.realcounter = 0
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


class UpdaterHistory(object):
  """Stores the history for what has been processed to date."""

  def __init__(self):
    self.latest_update = {}
    self.node_state = {}
    self.current_hour = None
    self.current_file = None
    self.current_file_lineno = None


class SDUpdater(object):
  """Updates SD based on a directory of logfiles."""

  def __init__(self, project, house, history_file, dry_run, debug=False):
    # self.history must be defined first to avoid infinite loop in setattr.
    self.project = project
    self.house = house
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
      self.history = UpdaterHistory()
    self.client = monitoring.Client(project=project)

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

  def ParseTempSensorLine(self, parts):
    bat = int(parts[0])
    temp = ParseFloat(parts, 1)
    return temp, bat

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
      if report.ping_id > (state.last_ping_id + 1):
        state.num_reports += report.ping_id - state.last_ping_id
    state.num_reports += 1
    state.received_reports += 1
    # Store state for future.
    state.last_ping_id = report.ping_id
    state.last_ts = report.ts

  def CalcHourlyAverageAndReset(self, node_id, state, just):
    a = '% 2d: ' % node_id
    if NODE_HANDLERS[node_id] == 'ProcessTempSensor':
      if len(state.temps) > 0:
        a += '%.02f°C' % (sum(state.temps) / len(state.temps))
        just += 1  # degree confuses ljust... sigh.
      state.temps = []
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
        if self.current_hour and report.hour != self.current_hour:
          self.PrintHourlyReport()
        self.current_hour = report.hour
        # Handle the line depending on the node type.
        handler = getattr(self,
            NODE_HANDLERS.get(report.node_id, 'IgnoreLine'))
        handler(report)
        # Keep stats about node report reliability every hour.
        self.UpdateNodeReport(report)
    # Print an update.
    self.PrintHourlyReport()
    # Save history
    self.SaveHistory()

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
    state = self.GetOrCreateNodeState(report.node_id)
    state.temps.append(temp)
    self.ReportMetric(report.node_id, TEMPERATURE, report.ts, temp)
    self.ReportMetric(report.node_id, BATTERY, report.ts, bat)

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

  def ReportMetric(self, node_id, metric, ts, value):
    delta = time.time() - ts
    if delta > 86400:
      print 'Skipping node %s:%s@%s - SD only accepts 24h of history' % (
          node_id, metric, ts)
      return

    write_data = {
        "timeSeries": [
          {
            "metric": {
              "type": metric,
              "labels": {
                "node_id": str(node_id),
                "house": self.house
              }
            },
            "resource": {
              "type": "global",
              "labels": {
                "project_id": self.project
              }
            },
            "points": [
              {
                "interval": {
                  "endTime": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(ts)),
                },
                "value": {
                  "doubleValue": value
                }
              }
            ]
          }
      ]
    }
    WRITE_PATH = '/projects/%s/timeSeries' % self.project
    if self.dry_run:
      print 'POST %s' % WRITE_PATH
      print write_data
    else:
      response = self.client.connection.api_request(
        method='POST', path=WRITE_PATH, data=write_data)
      pass


def main():
  parser = optparse.OptionParser()
  parser.add_option('--dry_run', action='store_true', dest='dry_run')
  parser.add_option('--debug', action='store_true', dest='debug')
  parser.add_option('--project', action='store', dest='project', default=None)
  parser.add_option('--house', action='store', dest='house', default=None)
  parser.add_option('--history_file', action='store', dest='history_file')
  options, args = parser.parse_args()
  if len(args) < 1:
    sys.stderr.write('Usage: %s [--dry_run] [--debug] [--history_file foo] '
        '--project p --house h logfile1 [logfile2, ...]\n' % sys.argv[0])
    sys.exit(1)

  if not options.project or not options.house:
    parser.error('Project and House must be specified')

  updater = SDUpdater(options.project, options.house,
      options.history_file, options.dry_run, options.debug)
  updater.ProcessFiles(args)


if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
