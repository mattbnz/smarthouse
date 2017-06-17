package main

import (
    "bytes"
    "encoding/binary"
    "fmt"
    "log"
    "net/http"
    "os"
    "time"
    "github.com/goburrow/modbus"
    "github.com/prometheus/client_golang/prometheus"
    "github.com/prometheus/client_golang/prometheus/promhttp"
)

// solarMatrics provide description, register #, and value type
type solarMetrics []struct {
    desc        *prometheus.Desc
    register    uint16
    valType     prometheus.ValueType
    fetcher     func(modbus.Client, uint16)(float64, error)
    multiplier  float64
}

type scrapedMetrics struct {
    u16     []float64
    u32     []float64
}

type solarCollector struct {
    upDesc      *prometheus.Desc
    metrics     solarMetrics
}

func NewSolarCollector() prometheus.Collector {
    return &solarCollector {
        upDesc: prometheus.NewDesc(
            "up",
            "Was the last scrape of the inverter successful.",
            nil, nil,
        ),
        metrics: solarMetrics{
            {
                desc: prometheus.NewDesc(
                    "dc_voltage",
                    "DC input voltage.",
                    nil, nil),
                register: 5011,
                valType: prometheus.GaugeValue,
                fetcher: fetchU16,
                multiplier: 0.1,
            }, {
                desc: prometheus.NewDesc(
                    "dc_current",
                    "DC input current.",
                    nil, nil),
                register: 5012,
                valType: prometheus.GaugeValue,
                fetcher: fetchU16,
                multiplier: 0.1,
            }, {
                desc: prometheus.NewDesc(
                    "upload_line_voltage",
                    "Upload line voltage.",
                    nil, nil),
                register: 5019,
                valType: prometheus.GaugeValue,
                fetcher: fetchU16,
                multiplier: 0.1,
            }, {
                desc: prometheus.NewDesc(
                    "upload_line_current",
                    "Upload line current.",
                    nil, nil),
                register: 5022,
                valType: prometheus.GaugeValue,
                fetcher: fetchU16,
                multiplier: 0.1,
            }, {
                desc: prometheus.NewDesc(
                    "power_factor",
                    "Current power factor.",
                    nil, nil),
                register: 5035,
                valType: prometheus.GaugeValue,
                fetcher: fetchS16,
                multiplier: 0.001,
            }, {
                desc: prometheus.NewDesc(
                    "grid_frequency",
                    "Observed grid frequency.",
                    nil, nil),
                register: 5036,
                valType: prometheus.GaugeValue,
                fetcher: fetchU16,
                multiplier: 0.1,
            }, {
                desc: prometheus.NewDesc(
                    "running_hours",
                    "Total running time of inverter..",
                    nil, nil),
                register: 5006,
                valType: prometheus.GaugeValue,
                fetcher: fetchU32,
                multiplier: 1,
			}, {
                desc: prometheus.NewDesc(
                    "dc_power_watts",
                    "Total DC power.",
                    nil, nil),
                register: 5017,
                valType: prometheus.GaugeValue,
                fetcher: fetchU32,
                multiplier: 1,
			}, {
                desc: prometheus.NewDesc(
                    "active_power_watts",
                    "Current active power output.",
                    nil, nil),
                register: 5031,
                valType: prometheus.GaugeValue,
                fetcher: fetchU32,
                multiplier: 1,
            },
        },
    }
}

func (c *solarCollector) Scrape() ([]float64, bool) {
    rtu := modbus.NewRTUClientHandler("/dev/ttyUSB0")
    rtu.BaudRate = 9600
    rtu.DataBits = 8
    rtu.Parity = "N"
    rtu.StopBits = 1
    rtu.SlaveId = 1
    rtu.Timeout = 1 * time.Second
    rtu.Logger = log.New(os.Stdout, "rtu", log.Lshortfile)

    rtu.Connect()
    defer rtu.Close()

    client := modbus.NewClient(rtu)

    missing := 0
    metrics := make([]float64, len(c.metrics))
    for n, m := range c.metrics {
        if (missing > 2) {
            fmt.Println("Declaring offline, abandoning scrape")
            return metrics, false
        }
		t, err := m.fetcher(client, m.register)
		if err != nil {
            metrics[n] = -1
            fmt.Printf("Failed to fetch %d: %s\n", m.register, err)
            missing += 1
		} else {
            metrics[n] = float64(t) * m.multiplier
        }
    }
	return metrics, true
}

func (c *solarCollector) Describe(ch chan<- *prometheus.Desc) {
    ch <- c.upDesc
    for _, i := range c.metrics {
        ch <- i.desc
    }
}

func (c*solarCollector) Collect(ch chan<- prometheus.Metric) {
    metrics, ok := c.Scrape()
    if ok == true {
        ch <- prometheus.MustNewConstMetric(c.upDesc, prometheus.GaugeValue, 1)
        for n, i := range c.metrics {
            ch <- prometheus.MustNewConstMetric(i.desc, i.valType, metrics[n])
        }
    } else {
        ch <- prometheus.MustNewConstMetric(c.upDesc, prometheus.GaugeValue, 0)
    }
}

func fetch(client modbus.Client, start uint16, count uint16) ([]byte, error) {
    var lasterr error
	// Modbus seems a bit unreliable, retry once on failure.
	for i := 0; i < 2; i++ {
        results, err := client.ReadInputRegisters(start-1, count)
        if err != nil {
            lasterr = err
            continue
        }
        if (len(results) != int(count*2)) {
            lasterr = fmt.Errorf(
                "Did not get expected %d bytes back! % x", count*2, results)
        }
        return results, nil
    }
    return nil, lasterr
}

func fetchU16(client modbus.Client, register uint16) (float64, error) {
    results, err := fetch(client, register, 1)
    if err != nil {
        fmt.Println("Could not retrieve u16: ", err)
		return 0, err
    }
    return float64(binary.BigEndian.Uint16(results)), nil
}

func fetchS16(client modbus.Client, register uint16) (float64, error) {
    results, err := fetch(client, register, 1)
    if err != nil {
        fmt.Println("Could not retrieve s16: ", err)
        return 0, err
    }
    var r int16;
    err = binary.Read(bytes.NewReader(results), binary.BigEndian, &r)
    if err != nil {
        return 0, err
    }
    return float64(r), nil
}

// Modbus encodes 32-bit values across 2 registers in Little Endian order, but
// the bytes in each individual register are Big Endian.
func leRegistersToBeBytes(bytes []byte) ([]byte) {
    t := bytes[0]
    bytes[0] = bytes[2]
    bytes[2] = t
    t = bytes[1]
    bytes[1] = bytes[3]
    bytes[3] = t
    return bytes
}

func fetchU32(client modbus.Client, register uint16) (float64, error) {
    results, err := fetch(client, register, 2)
    if err != nil {
        fmt.Println("Could not retrieve u32: ", err)
		return 0, err
    }
    return float64(binary.BigEndian.Uint32(leRegistersToBeBytes(results))), nil
}

func main() {
    collector := NewSolarCollector()
    prometheus.MustRegister(collector)

    http.Handle("/metrics", promhttp.Handler())
    http.ListenAndServe(":1510", nil)


}
