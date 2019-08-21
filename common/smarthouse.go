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
    "log"
    "net/http"
    "net/url"
    "time"
    "github.com/prometheus/client_golang/prometheus"
    "github.com/prometheus/client_golang/prometheus/promhttp"
    mqtt "github.com/eclipse/paho.mqtt.golang"

)

var httpPort = flag.Int("port", 1510, "HTTP port to listen on")
var mqttUrl = flag.String("mqtt_url", "tcp://localhost", "URL to the MQTT broker")

type waterCollector struct {
    pumpNum             int
    currentRate         prometheus.Gauge
    last60s_mL          prometheus.Counter
    total_mL            prometheus.Counter
    lastRecord          time.Time
    lastReceived        time.Time
    //TODO probably need a lock... 

}

func NewWaterCollector(pumpNum int) waterCollector {
    return waterCollector {
        pumpNum:        pumpNum,
        currentRate:    prometheus.NewGauge(prometheus.GaugeOpts{
            Name:     "mL_per_min",
            Help:     "current (instantaneous) rate of flow in mL/min",
            ConstLabels:   prometheus.Labels{"pump": fmt.Sprintf("%d", pumpNum)},
        }),
        last60s_mL:     prometheus.NewCounter(prometheus.CounterOpts{
            Name:     "last60s_mL",
            Help:     "last60s mL passed through sensor in the last minute",
           ConstLabels:   prometheus.Labels{"pump": fmt.Sprintf("%d", pumpNum)},
        }),
        total_mL:       prometheus.NewCounter(prometheus.CounterOpts{
            Name:     "total_mL",
            Help:     "total mL passed through sensor",
            ConstLabels:   prometheus.Labels{"pump": fmt.Sprintf("%d", pumpNum)},
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

    mqttClient.Subscribe(fmt.Sprintf("smarthouse/water/flow-meter/%d", c.pumpNum), 0,
        func(client mqtt.Client, msg mqtt.Message) {
            var report mqttReport
            err := json.Unmarshal([]byte(msg.Payload()), &report)
            if err != nil {
                log.Printf("Failed to Unmarshall received report (%s): %v", string(msg.Payload()), err)
                return;
            }
            log.Printf("Pump %d: Received MQTT report %s flow_rate=%f, flow_mL=%f, total_mL=%f",
                c.pumpNum, string(msg.Payload()), report.ML_per_min, report.Flow_mL, report.Total_mL)
            c.currentRate.Set(report.ML_per_min)
            c.last60s_mL.Add(report.Flow_mL)
            c.total_mL.Add(report.Flow_mL)
            c.lastReceived = time.Now()
        })
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

func main() {
    flag.Parse()

    uri, err := url.Parse(*mqttUrl)
    if err != nil {
        log.Fatal(err)
    }
    mqtt := mqttConnect("smarthouse", uri)
    water1 := NewWaterCollector(1)
    water1.Init(mqtt)
    water2 := NewWaterCollector(2)
    water2.Init(mqtt)

    log.Print("Starting HTTP server...")
    http.Handle("/metrics", promhttp.Handler())
    http.ListenAndServe(fmt.Sprintf(":%d", *httpPort), nil)
}
