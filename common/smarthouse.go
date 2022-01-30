// Copyright (C) 2019 Matt Brown. All rights reserved.

// Provides central reporting and logging for smarthouse components. 
// 
// Receives incoming data from MQTT and provides it to a metrics interface for
// ingestion into a real-time monitoring system (e.g. Prometheus) while also
// logging the raw value to a DB for historical analysis.
package main

import (
    "sync/atomic"
    "encoding/json"
    "flag"
    "fmt"
    "io"
    "log"
    "math"
    "net/http"
    "net/url"
    "os"
    "strings"
    "time"
    humanize "github.com/dustin/go-humanize"
    "github.com/prometheus/client_golang/prometheus"
    "github.com/prometheus/client_golang/prometheus/promhttp"
    mqtt "github.com/eclipse/paho.mqtt.golang"

)

var httpPort = flag.Int("port", 1510, "HTTP port to listen on")
var mqttUrl = flag.String("mqtt_url", "tcp://localhost", "URL to the MQTT broker")
var mqttClient mqtt.Client

type mqttHello struct {
    Received time.Time
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

type Node struct {
    Name string
    Config mqttHello
    LastContact time.Time
    Sensors []Sensor
}
var nodes = make(map[string]*Node)

type Sensor interface {
    Init(mqtt.Client)
    Shutdown()
    Describe(bool) string
}

type flowSensor struct {
    node                *Node
    flow                string
    lastReceived        time.Time
    // Prometheus exports
    p_mL_per_min        prometheus.GaugeFunc
    p_last60s_mL        prometheus.CounterFunc
    p_total_mL          prometheus.CounterFunc
    p_reportAge         prometheus.GaugeFunc
    // Actual counters (in bits)
    mL_per_min          uint64
    last60s_mL          uint64
    total_mL            uint64
}
func NewFlowSensor(node *Node, flow string) flowSensor {
    return flowSensor {
        node:           node,
        flow:           flow,
        mL_per_min:     0,
        last60s_mL:     0,
        total_mL:       0,
        lastReceived:   time.Unix(0, 0),
    }
}

type mqttReport struct {
    ML_per_min float64
    Flow_mL float64
    Total_mL float64
}

func (c *flowSensor) Init(mqttClient mqtt.Client) {
    labels := prometheus.Labels{"node": c.node.Name, "flow": c.flow}
    c.p_mL_per_min = prometheus.NewGaugeFunc(
        prometheus.GaugeOpts{
            Name:     "mL_per_min",
            Help:     "current (instantaneous) rate of flow in mL/min",
            ConstLabels:   labels,
        },
        func() float64 {
            return float64(math.Float64frombits(atomic.LoadUint64(&c.mL_per_min)))
        },
    )
    prometheus.MustRegister(c.p_mL_per_min) // TODO: Better error handling?
    c.p_last60s_mL = prometheus.NewCounterFunc(
        prometheus.CounterOpts{
            Name:     "last60s_mL",
            Help:     "mL passed through sensor in the last minute",
            ConstLabels:   labels,
        },
        func() float64 {
            return float64(math.Float64frombits(atomic.LoadUint64(&c.last60s_mL)))
        },
    )
    prometheus.MustRegister(c.p_last60s_mL) // TODO: Better error handling?
    c.p_total_mL = prometheus.NewCounterFunc(
        prometheus.CounterOpts{
            Name:     "total_mL",
            Help:     "total mL passed through sensor",
            ConstLabels:   labels,
        },
        func() float64 {
            return float64(math.Float64frombits(atomic.LoadUint64(&c.total_mL)))
        },
    )
    prometheus.MustRegister(c.p_total_mL) // TODO: Better error handling?
    c.p_reportAge = prometheus.NewGaugeFunc(
        prometheus.GaugeOpts{
            Name:     "report_age_s",
            Help:     "Seconds since the node last provided data",
            ConstLabels:    labels,
        },
        func() float64 {
            return time.Now().Sub(c.node.LastContact).Seconds()
        },
    )
    prometheus.MustRegister(c.p_reportAge) // TODO: Better error handling?

    topic := fmt.Sprintf("smarthouse/%s/flow-sensor/%s", c.node.Name, c.flow)
    log.Printf("Creating %s (%s)", c.Describe(false), topic)
    mqttClient.Subscribe(topic, 0, func(client mqtt.Client, msg mqtt.Message) {
            var report mqttReport
            err := json.Unmarshal([]byte(msg.Payload()), &report)
            n := time.Now()
            c.node.LastContact = n
            if err != nil {
                log.Printf("%s Flow %s:Failed to Unmarshall received report (%s): %v",
                    c.node.Name, c.flow, string(msg.Payload()), err)
                return;
            }
            log.Printf("%s Flow %s: Received MQTT report %s flow_rate=%f, flow_mL=%f, total_mL=%f",
                c.node.Name, c.flow, string(msg.Payload()), report.ML_per_min, report.Flow_mL,
                report.Total_mL)
            atomic.StoreUint64(&c.mL_per_min, math.Float64bits(report.ML_per_min))
            c.add(&c.last60s_mL, report.Flow_mL)
            c.add(&c.total_mL, report.Flow_mL)
            c.lastReceived = time.Now()
        })
}

func (c *flowSensor) add(v *uint64, i float64) {
    for {
        oBits := atomic.LoadUint64(v)
        nBits := math.Float64bits(math.Float64frombits(oBits) + i)
        if (atomic.CompareAndSwapUint64(v, oBits, nBits)) {
            return
        }
    }
}

func (c *flowSensor) get(v *uint64) float64 {
    return math.Float64frombits(atomic.LoadUint64(v))
}

func (c *flowSensor) Shutdown() {
    prometheus.Unregister(c.p_mL_per_min)
    prometheus.Unregister(c.p_last60s_mL)
    prometheus.Unregister(c.p_total_mL)
    prometheus.Unregister(c.p_reportAge)
    log.Printf("Shutdown %s", c.Describe(false))
}

func (c *flowSensor) Describe(all bool) string {
    s := fmt.Sprintf("FlowSensor for %s on %s", c.flow, c.node.Name)
    if (all) {
        s += fmt.Sprintf(" @ %s: mL_per_min=%f, last60s_mL=%f, total_mL=%f",
            c.lastReceived.Format(time.RFC3339), c.get(&c.mL_per_min),
            c.get(&c.last60s_mL), c.get(&c.total_mL))
    }
    return s
}

func GotHello(client mqtt.Client, msg mqtt.Message) {
    var hello mqttHello
    err := json.Unmarshal([]byte(msg.Payload()), &hello)
    if err != nil {
        log.Printf("Failed to Unmarshall received hello (%s): %v", string(msg.Payload()), err)
        return
    }
    hello.Received = time.Now()
    if _, ok := nodes[hello.Node]; ok {
        UpdateNode(hello)
    } else {
        RegisterNode(hello)
    }
}

func RegisterNode(config mqttHello) {
    log.Printf("Got hello from new node %s", config.Node)
    var node Node
    node.Name = config.Node
    node.Config = config
    node.LastContact = time.Now()
    SetupSensors(&node)
    nodes[node.Name] = &node
    node = *nodes[node.Name]  // Pick up new object from SetupSensors
    log.Printf("Registered %s with %d sensors", node.Name, len(node.Sensors))
}

func UpdateNode(config mqttHello) {
    log.Printf("Got hello from existing node %s", config.Node)
    node := nodes[config.Node]
    oldSpec := (*node).Config.SensorSpec
    (*node).Config = config
    if oldSpec != config.SensorSpec {
        SetupSensors(node)
    }
    (*node).LastContact = time.Now()
    log.Printf("Updated %s with %d sensors", config.Node, len(nodes[config.Node].Sensors))
}

func SetupSensors(node *Node) {
    // Delete any existing sensor definitions
    for _, s := range node.Sensors {
        s.Shutdown()
    }
    // Create the new
    node.Sensors = make([]Sensor, 0)
    // SensorSpec should conform to: NAME,TYPE,PIN[;NAME,TYPE,PIN]...
    for i, s := range strings.Split(node.Config.SensorSpec, ";") {
        t := strings.Split(s, ",")
        if len(t) < 3 {
            log.Printf("invalid SensorSpec (%s) at position %d for %s", s, i, node.Name)
            continue
        }
        // TODO: Support other sensor types (e.g. look at t[1])
        fs := NewFlowSensor(node, t[0])
        fs.Init(mqttClient)
        node.Sensors = append(node.Sensors, &fs)
    }
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
    for _, node := range nodes {
        hello := node.Config
        io.WriteString(w, "<h2>" + hello.Node + "</h2>")
        io.WriteString(w, "<b>Last Contact:</b>&nbsp;&nbsp;<span title=\"" +
            node.LastContact.Format(time.RFC3339) + "\">" +
            humanize.RelTime(node.LastContact, time.Now(), "ago", "in the future") + "</span>")
        io.WriteString(w, "<table><tr><th>Setting</th><th>Value</th></tr>")
        io.WriteString(w, "<tr><th>Received At</th><td>" + hello.Received.Format(time.RFC3339) + "</td></tr>")
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
        io.WriteString(w, "<ul>")
        for _, s := range node.Sensors {
            io.WriteString(w, "<li>" + s.Describe(true) + "</li>")
        }
        io.WriteString(w, "</ul>")
        io.WriteString(w, "<br/><br/>")
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

    mqtt.ERROR = log.New(os.Stdout, "[MQTT-ERROR] ", 0)
    mqtt.CRITICAL = log.New(os.Stdout, "[MQTT-CRIT] ", 0)
    mqtt.WARN = log.New(os.Stdout, "[MQTT-WARN]  ", 0)

    mqttClient = mqttConnect(fmt.Sprintf("smarthouse.%s", hostname), uri)

    mqttClient.Subscribe("smarthouse/hello", 0, GotHello)

    log.Print("Starting HTTP server...")
    http.Handle("/metrics", promhttp.Handler())
    http.HandleFunc("/nodes", reportNodes)
    http.ListenAndServe(fmt.Sprintf(":%d", *httpPort), nil)
}
