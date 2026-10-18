#!/usr/bin/env python3
"""Generates the Water Grafana dashboard (water-dashboard-v2.json).

    ./water-dashboard.py > water-dashboard-v2.json           # dark theme
    ./water-dashboard.py --light > water-dashboard-v2.json   # light theme

Colours are fixed (Grafana doesn't theme them), so the day x hour grids get a
ramp chosen for the theme in use: more water = brighter on dark, darker on
light.
"""
import json
import sys

DS = {"type": "prometheus", "uid": "bMQFsMInk"}
# Validated (dataviz validate_palette.js) for light and dark Grafana surfaces.
SITES = [
    # flow label, display name, colour, alert thresholds (L/min)
    ("house", "House", "#3987e5", [5, 10, 30]),
    ("farm", "Farm", "#d95926", [5, 15, 30]),
]
GRAY = "#8e8e8e"
# Litres-per-hour classes for the day x hour grids: one blue hue, brighter =
# more (validated as an ordinal ramp on Grafana's dark surface). 0 L is blank,
# so any overnight use stands out.
HOUR_LIMITS = [0.1, 10, 50, 100, 250]
HOUR_RAMP_DARK = ["#184f95", "#2a78d6", "#5598e7", "#86b6ef", "#cde2fb"]
HOUR_RAMP_LIGHT = ["#86b6ef", "#5598e7", "#2a78d6", "#1c5cab", "#0d366b"]
LIGHT = "--light" in sys.argv
HOUR_CLASSES = list(zip(HOUR_LIMITS, HOUR_RAMP_LIGHT if LIGHT else HOUR_RAMP_DARK))  # threshold lines: neutral, so they don't compete with site hues

_id = [0]


def nid():
    _id[0] += 1
    return _id[0]


def target(expr, legend="", ref="A", instant=False, interval=None, fmt=None):
    t = {"datasource": DS, "editorMode": "code", "expr": expr,
         "legendFormat": legend, "refId": ref, "range": not instant,
         "instant": instant}
    if interval:
        t["interval"] = interval
    if fmt:
        t["format"] = fmt
    return t


def site_overrides():
    # Colour follows the site everywhere it appears.
    return [{
        "matcher": {"id": "byName", "options": flow},
        "properties": [{"id": "color", "value": {"mode": "fixed", "fixedColor": colour}},
                       {"id": "displayName", "value": name}],
    } for flow, name, colour, _ in SITES]


def site_names():
    return [{"matcher": {"id": "byName", "options": flow},
             "properties": [{"id": "displayName", "value": name}]}
            for flow, name, _, _ in SITES]


def gray_thresholds(values):
    steps = [{"color": "transparent", "value": None}]
    steps += [{"color": GRAY, "value": v} for v in values]
    return {"mode": "absolute", "steps": steps}


def row(title, y, collapsed=False):
    return {"type": "row", "id": nid(), "title": title, "collapsed": collapsed,
            "gridPos": {"h": 1, "w": 24, "x": 0, "y": y}, "panels": []}


def stat(title, desc, targets, unit, decimals, x, y, w, h=5, time_from=None, spark=False):
    p = {
        "type": "stat", "id": nid(), "title": title, "description": desc,
        "datasource": DS, "targets": targets,
        "gridPos": {"h": h, "w": w, "x": x, "y": y},
        "fieldConfig": {
            "defaults": {"unit": unit, "decimals": decimals,
                         "color": {"mode": "fixed", "fixedColor": "text"},
                         "thresholds": {"mode": "absolute", "steps": [{"color": "text", "value": None}]}},
            "overrides": site_overrides(),
        },
        "options": {
            "colorMode": "none", "graphMode": "area" if spark else "none",
            "justifyMode": "auto", "orientation": "vertical", "textMode": "value_and_name",
            "wideLayout": True, "showPercentChange": False,
            "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False},
        },
    }
    if time_from:
        p["timeFrom"] = time_from
    return p


def timeseries(title, desc, targets, unit, x, y, w, h, colour=None, thresholds=None,
               interp="linear", bars=False, stack=False, legend=False, legend_calcs=None,
               time_from=None, interval=None, decimals=None, min0=True):
    custom = {
        "drawStyle": "bars" if bars else "line",
        "lineInterpolation": interp,
        "lineWidth": 0 if bars else 2,
        "fillOpacity": 90 if bars else 10,
        "gradientMode": "none" if bars else "opacity",
        "showPoints": "never",
        "spanNulls": False,
        "barAlignment": -1 if bars else 0,
        "barWidthFactor": 0.8,
        "stacking": {"mode": "normal" if stack else "none", "group": "A"},
        "axisBorderShow": False, "axisPlacement": "auto",
        "thresholdsStyle": {"mode": "dashed" if thresholds else "off"},
    }
    defaults = {"unit": unit, "custom": custom,
                "color": ({"mode": "fixed", "fixedColor": colour} if colour else {"mode": "palette-classic"}),
                "thresholds": gray_thresholds(thresholds or [])}
    if min0:
        defaults["min"] = 0
    if decimals is not None:
        defaults["decimals"] = decimals
    p = {
        "type": "timeseries", "id": nid(), "title": title, "description": desc,
        "datasource": DS, "targets": targets,
        "gridPos": {"h": h, "w": w, "x": x, "y": y},
        "fieldConfig": {"defaults": defaults, "overrides": [] if colour else site_overrides()},
        "options": {
            "legend": {"showLegend": legend, "displayMode": "table" if legend_calcs else "list",
                       "placement": "bottom", "calcs": legend_calcs or []},
            "tooltip": {"mode": "multi" if stack else "single", "sort": "desc", "hideZeros": False},
        },
    }
    if time_from:
        p["timeFrom"] = time_from
    if interval:
        p["interval"] = interval
    return p


def day_hour_grid(flow, name, x, y, w):
    """Rows = the same 24h window N days ago; columns = hour of day."""
    targets = []
    for n in range(14):
        off = f" offset {n}d" if n else ""
        targets.append(target(
            f'sum(increase(total_mL{{job="water",flow="{flow}"}}[1h]{off})) / 1000',
            legend="Today" if n == 0 else f"{n}d ago", ref=chr(ord("A") + n), interval="1h"))
    return {
        "type": "status-history", "id": nid(), "title": f"{name}: litres per hour, last 14 days",
        "description": "Each row is the same 24 hours, N days earlier; each cell is one hour. "
                       "Blank = no water; " + ("darker" if LIGHT else "brighter") + " = more (classes: >0, 10, 50, 100, 250 L). Look down a column to compare a time of day "
                       "across days (e.g. overnight usage = leak).",
        "datasource": DS, "targets": targets, "timeFrom": "24h",
        "gridPos": {"h": 11, "w": w, "x": x, "y": y},
        "fieldConfig": {
            "defaults": {"unit": "litre", "decimals": 0,
                         "color": {"mode": "thresholds"},
                         "thresholds": {"mode": "absolute", "steps":
                                        [{"color": "transparent", "value": None}] +
                                        [{"color": c, "value": v} for v, c in HOUR_CLASSES]},
                         "custom": {"lineWidth": 0, "fillOpacity": 90}},
            "overrides": [],
        },
        "options": {"showValue": "never", "rowHeight": 0.85, "colWidth": 0.9,
                    "legend": {"showLegend": True, "displayMode": "list", "placement": "bottom"},
                    "tooltip": {"mode": "single", "sort": "none", "hideZeros": False}},
    }


def build():
    panels = []
    y = 0
    sites_sum = 'sum by (flow) ({})'
    legend_site = "{{flow}}"

    # --- Now: headline numbers -------------------------------------------
    panels.append(row("Now", y)); y += 1
    panels.append(stat(
        "Flow now", "Current flow rate per site (10s resolution).",
        [target('sum by (flow) (mL_per_min{job="water"}) / 1000', legend_site)],
        "flowlpm", 1, 0, y, 6, spark=True))
    panels.append(stat(
        "Today", "Water used since local midnight.",
        [target('sum by (flow) (increase(total_mL{job="water"}[$__range])) / 1000', legend_site, "A", instant=True),
         target('sum(increase(total_mL{job="water"}[$__range])) / 1000', "Total", "B", instant=True)],
        "litre", None, 6, y, 6, time_from="now/d"))
    panels.append(stat(
        "Last 7 days", "Water used in the last 7 days (rolling).",
        [target('sum by (flow) (increase(total_mL{job="water"}[7d])) / 1000', legend_site, "A", instant=True),
         target('sum(increase(total_mL{job="water"}[7d])) / 1000', "Total", "B", instant=True)],
        "litre", None, 12, y, 6))
    panels.append(stat(
        "vs previous 7 days", "Change in the last 7 days' usage compared with the 7 days before.",
        [target('(sum by (flow) (increase(total_mL{job="water"}[7d])) / sum by (flow) (increase(total_mL{job="water"}[7d] offset 7d)) - 1) * 100',
                legend_site, "A", instant=True),
         target('(sum(increase(total_mL{job="water"}[7d])) / sum(increase(total_mL{job="water"}[7d] offset 7d)) - 1) * 100',
                "Total", "B", instant=True)],
        "percent", 0, 18, y, 6))
    y += 5

    # --- Per site: flow rate above pump power, shared crosshair -----------
    for flow, name, colour, thr in SITES:
        panels.append(row(name, y)); y += 1
        panels.append(timeseries(
            f"{name}: flow rate",
            "Average flow rate (L/min). Dashed lines are the alert levels: "
            f"{thr[0]} L/min for 60m, {thr[1]} L/min for {'20' if flow == 'house' else '30'}m, "
            f"{thr[2]} L/min for 5m.",
            [target(f'rate(total_mL{{job="water",flow="{flow}"}}[$__rate_interval]) * 60 / 1000', name)],
            "flowlpm", 0, y, 24, 8, colour=colour, thresholds=thr, decimals=1))
        y += 8
        panels.append(timeseries(
            f"{name}: pump power",
            "Pump power draw. Dashed line: 200 W 'pump running' alert level. "
            "Pump running with no flow above = check flow sensor, prime, pipes.",
            [target(f'esphome_sensor_value{{id="pump-{flow}_power"}}', f"{name} pump")],
            "watt", 0, y, 24, 5, colour=colour, thresholds=[200], interp="stepAfter", decimals=0))
        y += 5

    # --- History ---------------------------------------------------------
    panels.append(row("History", y)); y += 1
    panels.append(timeseries(
        "Daily usage, last 14 days",
        "Litres per day, house and farm stacked. Prometheus days are UTC, so each bar "
        "is the 24h ending at 1pm NZDT (noon NZST).",
        [target('sum by (flow) (increase(total_mL{job="water"}[1d])) / 1000', legend_site, interval="1d")],
        "litre", 0, y, 24, 9, bars=True, stack=True, legend=True,
        legend_calcs=["sum", "mean", "max"], time_from="14d", interval="1d", decimals=0))
    y += 9
    for i, (flow, name, _, _) in enumerate(SITES):
        panels.append(day_hour_grid(flow, name, 12 * i, y, 12))
    y += 11

    # --- Node health -----------------------------------------------------
    panels.append(row("Flow meter health", y, collapsed=False)); y += 1
    panels.append({
        "type": "stat", "id": nid(), "title": "Flow meters",
        "description": "Prometheus scrape status of each ESPHome flow meter.",
        "datasource": DS,
        "targets": [target(
            '(up{job="water"} * on (instance) group_left (flow) group by (instance, flow) '
            '(last_over_time(esphome_sensor_value{job="water",id="uptime"}[1d]))) or on (instance) up{job="water"}',
            "{{flow}}", instant=True)],
        "gridPos": {"h": 6, "w": 4, "x": 0, "y": y},
        "fieldConfig": {"defaults": {
            "mappings": [{"type": "value", "options": {
                "0": {"text": "DOWN", "color": "red", "index": 0},
                "1": {"text": "Up", "color": "green", "index": 1}}}],
            "color": {"mode": "thresholds"},
            "thresholds": {"mode": "absolute", "steps": [{"color": "red", "value": None}, {"color": "green", "value": 1}]},
        }, "overrides": site_names()},
        "options": {"colorMode": "value", "graphMode": "none", "textMode": "value_and_name",
                    "justifyMode": "auto", "orientation": "vertical", "wideLayout": True,
                    "showPercentChange": False,
                    "reduceOptions": {"calcs": ["lastNotNull"], "fields": "", "values": False}},
    })
    panels.append(timeseries(
        "WiFi signal", "Signal strength at each flow meter. Below about -80 dBm, expect dropouts.",
        [target('esphome_sensor_value{job="water",id="wifi_signal"}', "{{flow}}")],
        "dBm", 4, y, 8, 6, thresholds=[-80], legend=True, decimals=0, min0=False))
    panels.append({
        "type": "table", "id": nid(), "title": "Restarts",
        "description": "Restarts in the last 24h, uptime and the reason for the last restart.",
        "datasource": DS,
        "targets": [
            target('sum by (flow) (resets(esphome_sensor_value{job="water",id="uptime"}[24h]))', "", "A", instant=True, fmt="table"),
            target('max by (flow) (esphome_sensor_value{job="water",id="uptime"})', "", "B", instant=True, fmt="table"),
            target('max by (flow, value) (esphome_text_sensor_value{job="water",id="reset_reason"})', "", "C", instant=True, fmt="table"),
        ],
        "gridPos": {"h": 6, "w": 12, "x": 12, "y": y},
        "transformations": [
            {"id": "joinByField", "options": {"byField": "flow", "mode": "outer"}},
            {"id": "organize", "options": {
                "excludeByName": {"Time": True, "Time 1": True, "Time 2": True, "Time 3": True, "Value #C": True},
                "indexByName": {"flow": 0, "Value #A": 1, "Value #B": 2, "value": 3},
                "renameByName": {"flow": "Site", "Value #A": "Restarts (24h)",
                                 "Value #B": "Uptime", "value": "Last reset reason"}}},
        ],
        "fieldConfig": {"defaults": {"custom": {"align": "auto"}}, "overrides": [
            {"matcher": {"id": "byName", "options": "Site"},
             "properties": [{"id": "mappings", "value": [{"type": "value", "options": {
                 flow: {"text": name, "index": i} for i, (flow, name, _, _) in enumerate(SITES)}}]}]},
            {"matcher": {"id": "byName", "options": "Uptime"},
             "properties": [{"id": "unit", "value": "s"}, {"id": "decimals", "value": 0}]},
        ]},
        "options": {"showHeader": True, "cellHeight": "sm", "footer": {"show": False}},
    })
    y += 6

    return {
        "title": "Water",
        "uid": "water",
        "tags": ["water"],
        "timezone": "browser",
        "editable": True,
        "graphTooltip": 1,  # shared crosshair: line flow up with pump power
        "refresh": "30s",
        "schemaVersion": 40,
        "time": {"from": "now-24h", "to": "now"},
        "templating": {"list": []},
        "annotations": {"list": [
            {"builtIn": 1, "datasource": {"type": "grafana", "uid": "-- Grafana --"},
             "enable": True, "hide": True, "iconColor": "rgba(0, 211, 255, 1)",
             "name": "Annotations & Alerts", "type": "dashboard"},
            {"datasource": DS, "enable": True, "name": "Water alerts", "iconColor": "red",
             "expr": 'ALERTS{alertstate="firing",flow=~"house|farm"}', "step": "1m",
             "titleFormat": "{{alertname}}", "textFormat": "{{flow}}", "tagKeys": "flow",
             "useValueForTime": False},
        ]},
        "panels": panels,
    }


if __name__ == "__main__":
    json.dump(build(), sys.stdout, indent=2)
    sys.stdout.write("\n")
