package main

import (
    "bufio"
    "bytes"
    "encoding/binary"
    "fmt"
	"math"
    "os"
    "strconv"
    "strings"
    "time"
)

var density_air float32 = 1.2250
var specific_heat_air float32 = 1.006

var specific_heat = map[string]float32{
    // office
    "8": 3.08 * 3.08 * 2.5 * density_air * specific_heat_air * 1000,
    // boys
    "2": 3.63 * 3.08 * 2.5 * density_air * specific_heat_air * 1000,
    // spare
    "7": 2.7 * 3.08 * 2.5 * density_air * specific_heat_air * 1000,
    // master
    "9": 4.04 * 4.88 * 2.5 * density_air * specific_heat_air * 1000,
}

type WindowState struct {
    // Size of windows being processed
    win_secs int64
    // unix timestamp of current window start
    start_ts int64
    // node temperature at start ts
    start_degC float32
    // most recently seen node update ts
    last_ts int64

    // immediate temperature of outside & node.
    outside_degC float32
    node_degC float32
    // true if we've seen the node temperature increase
    node_heated bool
    // if we see invalid temperatures, ignore until it stabilizes
    ignore int64
    // cumulative moving average of node-outside diff
    // within the window.
    diff float32
    points int

    // collection of transfer rates
    transfer_start_ts int64
    transfer_rates []float32
}

func check(e error) {
    if e != nil {
        panic(e)
    }
}

func AtoByte(s string) byte {
    i, err := strconv.Atoi(s)
    check(err)
    return byte(rune(i))
}

func ParseTemperature(parts []string) float32 {
    foo := make([]byte, 4)
    foo[0] = AtoByte(parts[9])
    foo[1] = AtoByte(parts[10])
    foo[2] = AtoByte(parts[11])
    foo[3] = AtoByte(parts[12])
    var t float32
    err := binary.Read(bytes.NewReader(foo), binary.LittleEndian, &t)
    check(err)
    return t
}

func sum(numbers []float32) (total float32) {
    for _, x := range numbers {
        total += x
    }
    return total
}

func median(numbers []float32) float32 {
    middle := len(numbers) / 2
    result := numbers[middle]
    if len(numbers)%2 == 0 {
        result = (result + numbers[middle-1]) / 2
    }
    return result
}

func stdDev(numbers []float32, mean float32) float32 {
    if len(numbers) <= 1 {
        return 0
    }
    total := 0.0
    for _, number := range numbers {
        total += math.Pow(float64(number-mean), 2)
    }
    variance := float32(total) / float32(len(numbers)-1)
    return float32(math.Sqrt(float64(variance)))
}

func min(numbers []float32) (min float32) {
    min = numbers[0]
    for _, number := range numbers {
        if number < min {
            min = number
        }
    }
    return min
}

func max(numbers []float32) (max float32) {
    max = numbers[0]
    for _, number := range numbers {
        if number > max {
            max = number
        }
    }
    return max
}

func checkTransferRates(numbers []float32) string {
    for _, number := range numbers {
        if number > 0 {
            return ""
        }
    }
    return "*"
}

func EndWindow(state *WindowState, nodeID string, last_ts int64) {
    _, verbose := os.LookupEnv("VERBOSE")
    var ts, period int64
	//var period int64
    if last_ts != -1 {
        ts = last_ts
        period = last_ts - state.start_ts
    } else {
        ts = state.start_ts + state.win_secs
        period = state.win_secs
    }
    if state.start_degC != -1 {
        change := state.node_degC - state.start_degC
        heat_transfer := (change * specific_heat[nodeID]) /
                        (state.diff * float32(period))
        timeobj := time.Unix(state.start_ts, 0)
        if timeobj.Hour() >= 1 && timeobj.Hour() < 7 {
            if ! state.node_heated {
                if (state.transfer_start_ts == -1 && heat_transfer < 0) {
                    state.transfer_start_ts = state.start_ts
                }
                if (state.transfer_start_ts != -1) {
                    state.transfer_rates = append(state.transfer_rates, heat_transfer)
                }
            }
        } else {
            if len(state.transfer_rates) > 0 {
                min := min(state.transfer_rates)
                avg := sum(state.transfer_rates) / float32(len(state.transfer_rates))
                med := median(state.transfer_rates)
                stddev := stdDev(state.transfer_rates, avg)
                max := max(state.transfer_rates)
                flag := checkTransferRates(state.transfer_rates)
                fmt.Printf("%s-%s % .1f % .1f % .1f % .1f % .1f %s\n",
                           time.Unix(state.transfer_start_ts, 0), timeobj, min, avg,
                            med, stddev, max, flag)
                state.transfer_start_ts = -1
                state.transfer_rates = make([]float32, 0)
            }
        }
        if verbose {
            if state.node_heated {
                fmt.Printf("%s heated % .1f % .1f\n", time.Unix(ts, 0), state.node_degC,
                           state.diff)
            } else {
                fmt.Printf("%s % .1f % .1f % .1f % .1f\n", time.Unix(ts, 0), heat_transfer,
                           state.node_degC, change, state.diff)
            }
        }
    } else {
        // No points in period
        if verbose {
            fmt.Printf("%s -\n", time.Unix(ts, 0))
        }
    }
    ts += 1
    state.start_degC = -1
    state.diff = -1
    state.points = 0
    state.start_ts += state.win_secs
    state.node_heated = false
}

func ScanFile(filename string, nodeID string,
              outsideID string, state *WindowState) {
    temps, err := os.Open(filename)
    check(err)
    scanner := bufio.NewScanner(temps)

    for scanner.Scan() {
        parts := strings.Split(scanner.Text(), " ")
        if parts[1] != "OK" {
            // Skip invalid lines.
            continue
        }
        ts, err := strconv.ParseInt(parts[0], 10, 64)
        check(err)
        if state.start_ts == -1 {
            state.start_ts = (ts / state.win_secs) * state.win_secs
            fmt.Println(state.win_secs, "second windows, starting",
                        time.Unix(state.start_ts, 0))
            state.start_degC = -1
            state.diff = -1
            state.points = 0
        } else if (state.start_ts + state.win_secs <= ts) {
            // Window reset
            EndWindow(state, nodeID, -1)
        }
        if parts[2] == outsideID {
            t := ParseTemperature(parts)
            if t != -127 {
                state.outside_degC = t
            } else {
                continue
            }
            //fmt.Println(time.Unix(ts, 0), "outside", outside)
        } else if parts[2] == nodeID {
            t := ParseTemperature(parts)
            //fmt.Println(time.Unix(ts, 0), "node", t)
            if t != -127 {
                if state.ignore <= 0 {
                    diff := t - state.node_degC
                    if diff > 0.0625 {
                        state.node_heated = true
                    }
                    state.node_degC = t
                    if state.start_degC == -1 {
                        state.start_degC = state.node_degC
                    }
                    state.last_ts = ts
                }
                state.ignore -= 1
            } else {
                state.ignore = 15
                continue
            }
        } else {
            // Skip lines not matching specified node.
            continue
        }
        if state.outside_degC == -1 || state.node_degC == -1 {
            continue
        }
        if state.points > 0 {
            state.diff = ((state.node_degC - state.outside_degC) +
                          (float32(state.points) * state.diff)) /
                         float32(state.points  + 1)
        } else {
            state.diff = state.node_degC - state.outside_degC
        }
        state.points++
    }
}

func main() {

    if len(os.Args) < 5 {
        panic("Usage: node outside_node window_mins file [file, ...]")
    }
    var state WindowState

    window, err := strconv.ParseInt(os.Args[3], 10, 64)
    check(err)

    state.win_secs = window * 60
    state.start_ts = -1
    state.start_degC = -1
    state.last_ts = -1
    state.outside_degC = -1
    state.node_degC = -1
    state.node_heated = false
    state.ignore = 0
    state.diff = -1
    state.points = -1
    state.transfer_start_ts = -1

    for _, filename := range os.Args[4:] {
        ScanFile(filename, os.Args[1], os.Args[2], &state)
    }
    // Final partial window
    if state.start_ts != -1 {
        EndWindow(&state, os.Args[1], state.last_ts)
    }
}
