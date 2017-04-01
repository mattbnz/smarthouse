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
import common
import optparse
import os
import rrdtool
import sys

START_TS = 1351378113
# Assuming 60s step size.
RRA_LAST = 'RRA:LAST:0.9:1:2628000'    # 5 years of exact measurements.
RRA_5 = 'RRA:AVERAGE:0.9:5:1051200'    # 10 years of 5min averages.
RRA_60 = 'RRA:AVERAGE:0.9:60:87600'    # 10 years of 1hr averages.
RRAS = (RRA_LAST, RRA_5, RRA_60)


class RRDUpdater(common.Updater):
  """Updates RRDs based on a directory of logfiles."""

  def __init__(self, state_dir, dry_run, debug=False):
    # self.history must be defined first to avoid infinite loop in setattr.
    self.rrds = []
    self.update_ts = None
    self.update_queue = {}
    super(RRDUpdater, self).__init__(state_dir, 'rrd-history.pickle', dry_run, debug)

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
        sys.stderr.write('ERROR: Could not create rrd %s for %s: %s\n' %
            (rrdfile, ds, e))
        sys.exit(1)
    self.rrds.append(rrdfile)
    print 'Created new RRD %s' % rrdfile

  def RRDForDs(self, ds):
    return os.path.join(self.state_dir, '%s.rrd' % ds)

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
        if not self.dry_run:
          rrdtool.update(rrd, '-t', ':'.join(keys),
              '%s:%s' % (int(self.update_ts), datastr))
        elif self.debug:
          print ('rrdtool update -t', ':'.join(keys),
              '%s:%s' % (int(self.update_ts), datastr))
      except rrdtool.error, e:
        print e, 'from', self.update_queue, 'at', self.current_line
    self.update_queue = {}

  def LastUpdateFor(self, rrd):
    if self.dry_run and not os.path.exists(rrd):
      return 0
    if rrd not in self.latest_update:
      self.latest_update[rrd] = rrdtool.last(rrd)
    return self.latest_update[rrd]

  def FinishedProcessing(self):
    # Make sure the last report gets flushed.
    self.FlushUpdateQueue()
    # and whatever else our parent does.
    super(RRDUpdater, self).FinishedProcessing()

  def ReportMetric(self, node_id, metric, ts, value):
    data = {'node%d_%s' % (node_id, metric): value}
    self.UpdateRRD(ts, data)


def main():
  parser = optparse.OptionParser()
  parser.add_option('--dry_run', action='store_true', dest='dry_run')
  parser.add_option('--debug', action='store_true', dest='debug')
  parser.add_option('--state_dir', action='store', dest='state_dir')
  options, args = parser.parse_args()
  if len(args) < 2:
    sys.stderr.write('Usage: %s [--dry_run] [--debug] [--state_dir foo] '
        'logfile1 [logfile2, ...]\n' % sys.argv[0])
    sys.exit(1)

  updater = RRDUpdater(options.state_dir, options.dry_run, options.debug)
  updater.ProcessFiles(args)
  updater.PrintMeterSummary()

if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
