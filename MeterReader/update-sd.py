#!/usr/bin/python
# vim: set fileencoding=utf8
# 
# Copyright (C) 2016 - Matt Brown
#
# All rights reserved.
#
# Reads logger.py output and pushes to SD
import common
import gcloud
from gcloud import monitoring
import optparse
import os
import sys
import time

METRIC_MAP = {
    common.TEMPERATURE: 'custom.googleapis.com/smarthouse/temperature',
    common.BATTERY: 'custom.googleapis.com/smarthouse/battery',
}

class SDUpdater(common.Updater):
  """Updates SD based on a directory of logfiles."""

  def __init__(self, project, house, state_dir, dry_run, debug=False):
    # self.history must be defined first to avoid infinite loop in setattr.
    self.project = project
    self.house = house
    self.client = monitoring.Client(project=project)
    super(SDUpdater, self).__init__(state_dir, 'sd-history.pickle', dry_run, debug)

  def ReportMetric(self, node_id, metric, ts, value):
    sd_metric = METRIC_MAP.get(metric, None)
    if not sd_metric:
      return

    delta = time.time() - ts
    if delta > 86400:
      print 'Skipping node %s:%s@%s - SD only accepts 24h of history' % (
          node_id, metric, ts)
      return

    write_data = {
        "timeSeries": [
          {
            "metric": {
              "type": sd_metric,
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
      try:
        response = self.client.connection.api_request(
            method='POST', path=WRITE_PATH, data=write_data)
      except gcloud.exceptions.GCloudError, e:
        print 'Failed to write (%s:%s@%s): ' % (node_id, metric, ts), e


def main():
  parser = optparse.OptionParser()
  parser.add_option('--dry_run', action='store_true', dest='dry_run')
  parser.add_option('--debug', action='store_true', dest='debug')
  parser.add_option('--project', action='store', dest='project', default=None)
  parser.add_option('--house', action='store', dest='house', default=None)
  parser.add_option('--state_dir', action='store', dest='state_dir')
  options, args = parser.parse_args()
  if len(args) < 1:
    sys.stderr.write('Usage: %s [--dry_run] [--debug] [--state_dir foo] '
        '--project p --house h logfile1 [logfile2, ...]\n' % sys.argv[0])
    sys.exit(1)

  if not options.project or not options.house:
    parser.error('Project and House must be specified')

  updater = SDUpdater(options.project, options.house,
      options.state_dir, options.dry_run, options.debug)
  updater.ProcessFiles(args)


if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
