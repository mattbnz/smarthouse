package main

import (
    "encoding/binary"
    "fmt"
    "log"
    "net/http"
    "os"
    "time"
    "github.com/goburrow/modbus"
)

// Modbus/Sungrow encodes Uint32 across 2 16-bit registers, each individual
// register is Big Endian, but the 2 registers are in Little Endian to keep
// things interesting. I don't think go has a function to deal with this, so
// swap the bytes, before decoding as Big Endian.
func decodeUint32(b[] byte) uint32 {
    t := b[0]
    b[0] = b[2]
    b[2] = t
    t = b[1]
    b[1] = b[3]
    b[3] = t
    return binary.BigEndian.Uint32(b)
}

func handler(w http.ResponseWriter, r *http.Request) {
    rtu := modbus.NewRTUClientHandler("/dev/ttyUSB0")
    rtu.BaudRate = 9600
    rtu.DataBits = 8
    rtu.Parity = "N"
    rtu.StopBits = 1
    rtu.SlaveId = 1
    rtu.Timeout = 5 * time.Second
    rtu.Logger = log.New(os.Stdout, "rtu", log.Lshortfile)

    rtu.Connect()
    defer rtu.Close()

    client := modbus.NewClient(rtu)
    results, err := client.ReadInputRegisters(5030, 2)
    if err != nil {
        fmt.Fprintf(w, "Could not read registers! %s", err)
    }
    if (len(results) != 4) {
        fmt.Fprintf(w, "Did not get expected 4 bytes back! % x", results)
    }
    fmt.Fprintf(w, "Power output is %d", decodeUint32(results))
}

func main() {
    http.HandleFunc("/", handler)
    http.ListenAndServe(":1510", nil)
}
