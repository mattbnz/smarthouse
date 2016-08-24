#!/usr/bin/python
# vim: set fileencoding=utf8
# 
# Copyright (C) 2016 - Matt Brown
#
# All rights reserved.
#
# Generates graphs
from mako.template import Template
import optparse
import os
import rrdtool
import sys
import time

HOURS = [4, 12, 24, 48, 168, 336, 672]
GRAPH_W = 500
GRAPH_H = 300
COLORS = ['#ff0000', '#00ff00', '#0000ff', '#ff8c00', '#00ffff']


def timesince(d):
  delta = float(d)
  if delta < 120:
    return '%d seconds ago' % (delta)
  elif delta < 3600:
    return '%d minutes ago' % (delta/60)
  else:
    return '%d hours ago' % (delta/3600)


def DailyValue(rrd_filename, val_name, CF):
  args = ('x', '-s', 'now-24h', '-e', 'now',
      'DEF:val=%s:%s:LAST' % (rrd_filename, val_name),
      'VDEF:min=val,%s' % CF,'PRINT:min:%.2lf')
  _, _, values = rrdtool.graph(*args)
  if values: return values[-1]


def LoadConfig(rrd_dir):
  nodes = {}
  with open(os.path.join(rrd_dir,'config'), 'r') as fp:
    while True:
      line = fp.readline()
      if not line:
        break
      node_id, node_type, description = line.split(' ')
      d = {'type':node_type, 'desc':description}
      # Extract battery and other state
      rrd_file = os.path.join(rrd_dir, 'node%s_bat.rrd' % node_id)
      v = DailyValue(rrd_file, 'node%s_bat' % node_id, 'LAST')
      if v:
        d['bat'] = (float(v)+50)*20/1000.0
      else:
        d['bat'] = 0.0
      last_report = rrdtool.last(rrd_file)
      d['last_report'] = last_report
      d['report_delta'] = time.time() - last_report
      if node_type == 'TempSensor':
        rrd_file = os.path.join(rrd_dir, 'node%s_temp.rrd' % node_id)
        val_name = 'node%s_temp' % node_id
        d['temp'] = DailyValue(rrd_file, val_name, 'LAST')
        d['temp_24h_max'] = DailyValue(rrd_file, val_name, 'MAXIMUM')
        d['temp_24h_avg'] = DailyValue(rrd_file, val_name, 'AVERAGE')
        d['temp_24h_min'] = DailyValue(rrd_file, val_name, 'MINIMUM')
      nodes[int(node_id)] = d

  return nodes


def BatteryGraph(hours, nodes, graph_dir, rrd_dir):
  args = [os.path.join(graph_dir, 'battery-%dh.png' % hours),
      '--start', 'end-%dh' % hours, '--lower-limit', '0',
      '--title', 'Battery voltage']
  for n in nodes:
    args.append('DEF:node%d=%s/node%d_bat.rrd:node%d_bat:AVERAGE' %
        (n, rrd_dir, n, n))
  args.extend(['--vertical-label', 'mV', '--width', str(GRAPH_W),
               '--height', str(GRAPH_H)])
  for n in nodes:
    args.append('CDEF:mv%d=node%d,50,+,20,*' % (n, n))
  for n, d in nodes.iteritems():
    args.append('LINE1:mv%d%s:%s' % (n, COLORS[n-1], d['desc']))
  rrdtool.graph(*args)


def TemperatureGraph(hours, nodes, graph_dir, rrd_dir):
  args = [os.path.join(graph_dir, 'temp-%dh.png' % hours),
      '--start', 'end-%dh' % hours, '--lower-limit', '0',
      '--title', 'Temperature']
  for n in nodes:
    args.append('DEF:node%d=%s/node%d_temp.rrd:node%d_temp:AVERAGE' %
        (n, rrd_dir, n, n))
  args.extend(['--vertical-label', 'DegC', '--width', str(GRAPH_W),
               '--height', str(GRAPH_H)])
  for n, d in nodes.iteritems():
    args.append('LINE1:node%d%s:%s' % (n, COLORS[n-1], d['desc']))
  rrdtool.graph(*args)


def UpdateTemplate(graph_dir, nodes):
  data = {
      'now': time.strftime('%Y-%m-%d %H:%M:%S %Z'),
      'nodes': nodes,
      'timesince': timesince,
  }
  t = Template(filename=os.path.join(graph_dir, 'index.mako'))
  with open(os.path.join(graph_dir, 'index.html'), 'w') as fp:
    fp.write(t.render_unicode(**data).encode('utf-8'))


def main():
  parser = optparse.OptionParser()
  options, args = parser.parse_args()
  if len(args) < 2:
    sys.stderr.write('Usage: %s rrd_dir graph_dir\n' % sys.argv[0])
    sys.exit(1)

  nodes = LoadConfig(args[0])
  for hour in HOURS:
    BatteryGraph(hour, nodes, args[1], args[0])
    TemperatureGraph(hour, nodes, args[1], args[0])
  UpdateTemplate(args[1], nodes)


if __name__ == "__main__":
  main()

# Vim modeline
# vim: set ts=2 sw=2 sts=2 et: 
