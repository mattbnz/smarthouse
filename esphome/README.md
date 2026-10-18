ESPHome nodes
=============

ESPHome replacement for `WaterFlowMeter/` firmware and the `common/smarthouse.go`
receiver. Nodes serve `/metrics` and Prometheus scrapes them directly (see
`prometheus.yml`).

Any of the D1 mini variants (ESP-12F module, D1 mini Pro, diagonal-chip clone)
can run any node config; identity (name, static IP) lives in the config, not
the board.

Setup
-----

    pip install esphome
    cp secrets.yaml.example secrets.yaml   # and fill in

First flash over USB (replaces the old firmware), later updates via OTA:

    esphome run pumpmon-house.yaml                         # USB, pick port
    esphome run pumpmon-house.yaml --device 10.230.2.233   # OTA

Evaluate a spare board with `esphome run bench.yaml`, then watch
`http://bench.local/` for WiFi signal, reset reason and free heap.

Wiring
------

Flow sensor signal on **D5**. Never use D3, D4 or D8 (boot strapping pins) or
D0/A0 (no interrupt).

D1 mini Pro: antenna is selected by a 0 ohm resistor next to the ceramic
antenna; if it's set to the external connector, an antenna must be attached.
