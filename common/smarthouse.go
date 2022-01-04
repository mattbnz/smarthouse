// Copyright (C) 2019 Matt Brown. All rights reserved.

// Provides central reporting and logging for smarthouse components. 
// 
// Receives incoming data from MQTT and provides it to a metrics interface for
// ingestion into a real-time monitoring system (e.g. Prometheus) while also
// logging the raw value to a DB for historical analysis.
package main

import (
    "encoding/json"
    "flag"
    "fmt"
    "io"
    "log"
    "net/http"
    "net/url"
    "os"
    "time"
    "github.com/prometheus/client_golang/prometheus"
    "github.com/prometheus/client_golang/prometheus/promhttp"
    mqtt "github.com/eclipse/paho.mqtt.golang"

)

var httpPort = flag.Int("port", 1510, "HTTP port to listen on")
var mqttUrl = flag.String("mqtt_url", "tcp://localhost", "URL to the MQTT broker")

type waterCollector struct {
    nodeName            string
    flowNum             int
    currentRate         prometheus.Gauge
    last60s_mL          prometheus.Counter
    total_mL            prometheus.Counter
    lastRecord          time.Time
    lastReceived        time.Time
}

type mqttHello struct {
    Node string
    Version string
    Ip string
    LowPower int
    OtaStatus string
    WifiSSID string
    SensorSpec string
    ReportInterval int
    ReportCount int
    SleepInterval int
}
var hellos = make(map[string]mqttHello)

func NewWaterCollector(nodeName string, flowNum int) waterCollector {
    labels := prometheus.Labels{"node": nodeName, "flow": fmt.Sprintf("%d", flowNum)}
    return waterCollector {
        nodeName:       nodeName,
        flowNum:        flowNum,
        currentRate:    prometheus.NewGauge(prometheus.GaugeOpts{
            Name:     "mL_per_min",
            Help:     "current (instantaneous) rate of flow in mL/min",
            ConstLabels:   labels,
        }),
        last60s_mL:     prometheus.NewCounter(prometheus.CounterOpts{
            Name:     "last60s_mL",
            Help:     "last60s mL passed through sensor in the last minute",
            ConstLabels:   labels,
        }),
        total_mL:       prometheus.NewCounter(prometheus.CounterOpts{
            Name:     "total_mL",
            Help:     "total mL passed through sensor",
            ConstLabels:   labels,
        }),
    }
}

type mqttReport struct {
    ML_per_min float64
    Flow_mL float64
    Total_mL float64
}

func (c *waterCollector) Init(mqttClient mqtt.Client) {
    prometheus.MustRegister(c.currentRate)
    prometheus.MustRegister(c.last60s_mL)
    prometheus.MustRegister(c.total_mL)

    topic := fmt.Sprintf("smarthouse/%s/flow-meter/%d", c.nodeName, c.flowNum)
    if c.flowNum > 2 {
        // TODO: What a hack... clean this up.
        topic = fmt.Sprintf("smarthouse/%s/flow-sensor/%d", c.nodeName, c.flowNum)
    }
    log.Printf("Subscribing to receive updates for %s", topic)
    mqttClient.Subscribe(topic, 0, func(client mqtt.Client, msg mqtt.Message) {
            var report mqttReport
            err := json.Unmarshal([]byte(msg.Payload()), &report)
            if err != nil {
                log.Printf("Failed to Unmarshall received report (%s): %v", string(msg.Payload()), err)
                return;
            }
            log.Printf("%s Flow %d: Received MQTT report %s flow_rate=%f, flow_mL=%f, total_mL=%f",
                c.nodeName, c.flowNum, string(msg.Payload()), report.ML_per_min, report.Flow_mL,
                report.Total_mL)
            c.currentRate.Set(report.ML_per_min)
            c.last60s_mL.Add(report.Flow_mL)
            c.total_mL.Add(report.Flow_mL)
            c.lastReceived = time.Now()
        })
}

func GotHello(client mqtt.Client, msg mqtt.Message) {
    var hello mqttHello
    err := json.Unmarshal([]byte(msg.Payload()), &hello)
    if err != nil {
        log.Printf("Failed to Unmarshall received hello (%s): %v", string(msg.Payload()), err)
        return
    }
    log.Printf("Got hello from %s", hello.Node)
    hellos[hello.Node] = hello
}

func mqttConnect(clientId string, uri *url.URL) mqtt.Client {
	opts := createClientOptions(clientId, uri)
	client := mqtt.NewClient(opts)
	token := client.Connect()
	for !token.WaitTimeout(3 * time.Second) {
	}
	if err := token.Error(); err != nil {
		log.Fatal(err)
	}
	return client
}

func createClientOptions(clientId string, uri *url.URL) *mqtt.ClientOptions {
	opts := mqtt.NewClientOptions()
	opts.AddBroker(fmt.Sprintf("tcp://%s", uri.Host))
	opts.SetUsername(uri.User.Username())
	password, _ := uri.User.Password()
	opts.SetPassword(password)
	opts.SetClientID(clientId)
	return opts
}

func reportNodes(w http.ResponseWriter, _ *http.Request) {
    io.WriteString(w,"<html><body><H1>Nodes</H1>")
    for _, hello := range hellos {
        io.WriteString(w, "<h2>" + hello.Node + "</h2><table><tr><th>Setting</th><th>Value</th></tr>")
        io.WriteString(w, "<tr><th>Version</th><td>" + hello.Version + "</td></tr>")
        io.WriteString(w, "<tr><th>IP</th><td>" + hello.Ip + "</td></tr>")
        io.WriteString(w, "<tr><th>WifiSSID</th><td>" + hello.WifiSSID + "</td></tr>")
        io.WriteString(w, "<tr><th>SensorSpec</th><td>" + hello.SensorSpec + "</td></tr>")
        io.WriteString(w, "<tr><th>OtaStatus</th><td>" + hello.OtaStatus + "</td></tr>")
        io.WriteString(w, "<tr><th>ReportInterval</th><td>")
        io.WriteString(w, fmt.Sprintf("%d", hello.ReportInterval));
        io.WriteString(w,"</td></tr>")
        io.WriteString(w, "<tr><th>LowPower</th><td>")
        io.WriteString(w, fmt.Sprintf("%d", hello.LowPower));
        io.WriteString(w,"</td></tr>")
        if (hello.LowPower == 1) {
            io.WriteString(w, "<tr><th>ReportCount</th><td>")
            io.WriteString(w, fmt.Sprintf("%d", hello.ReportCount));
            io.WriteString(w,"</td></tr>")
            io.WriteString(w, "<tr><th>SleepInterval</th><td>")
            io.WriteString(w, fmt.Sprintf("%d", hello.SleepInterval));
            io.WriteString(w,"</td></tr>")
        }
        io.WriteString(w, "</table>")
    }
    io.WriteString(w, "</body></html>")
}

func main() {
    flag.Parse()

    uri, err := url.Parse(*mqttUrl)
    if err != nil {
        log.Fatal(err)
    }
    hostname, err := os.Hostname()
    if err != nil {
        log.Fatal(err)
    }
    mqtt := mqttConnect(fmt.Sprintf("smarthouse.%s", hostname), uri)

    mqtt.Subscribe("smarthouse/hello", 0, GotHello)

    // TODO: This needs a config file...
    pump1 := NewWaterCollector("pumpmon", 15)
    pump1.Init(mqtt)
    pump2 := NewWaterCollector("pumpmon", 14)
    pump2.Init(mqtt)
    spring1 := NewWaterCollector("spring", 1)
    spring1.Init(mqtt)
    spring2 := NewWaterCollector("spring", 2)
    spring2.Init(mqtt)
    spring3 := NewWaterCollector("springmon", 1)
    spring3.Init(mqtt)
    spring4 := NewWaterCollector("springmon", 2)
    spring4.Init(mqtt)
    house1 := NewWaterCollector("housepump", 1)
    house1.Init(mqtt)
    house2 := NewWaterCollector("housepump", 2)
    house2.Init(mqtt)

    log.Print("Starting HTTP server...")
    http.Handle("/metrics", promhttp.Handler())
    http.HandleFunc("/nodes", reportNodes)
    http.ListenAndServe(fmt.Sprintf(":%d", *httpPort), nil)
}
