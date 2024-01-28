package main

import (
	"fmt"
	"net/http"
	"path"
	"strconv"
	"time"

	"github.com/lorenzodonini/ocpp-go/ocpp1.6/core"
	"github.com/lorenzodonini/ocpp-go/ocpp1.6/types"
)

func init() {

	http.HandleFunc("/", Index)
	http.HandleFunc("/reset/", Reset)
	http.HandleFunc("/stop/", StopTransaction)
	http.HandleFunc("/start/", StartTransaction)
}

func Index(w http.ResponseWriter, r *http.Request) {

	w.Write([]byte(`<html><head><meta http-equiv="refresh" content="2; url=/"><title>MG Controller</title></head><body>`))

	for id, cp := range handler.chargePoints {
		w.Write([]byte(fmt.Sprintf("<h1>%s</h1>", id)))
		w.Write([]byte("<b>Vendor:</b> " + cp.BootData.ChargePointVendor + "<br>"))
		w.Write([]byte("<b>Model:</b> " + cp.BootData.ChargePointModel + "<br>"))
		w.Write([]byte("<b>Serial #:</b> " + cp.BootData.ChargePointSerialNumber + "<br>"))
		w.Write([]byte("<b>Firmware Ver:</b> " + cp.BootData.FirmwareVersion + "<br>"))

		w.Write([]byte("<b>Status:</b> " + cp.Status + "<br>"))
		w.Write([]byte("<b>Diag Status:</b> " + cp.DiagnosticsStatus + "<br>"))
		w.Write([]byte("<b>Firmware Status:</b> " + cp.FirmwareStatus + "<br>"))

		w.Write([]byte(fmt.Sprintf(`<form method="post" action="/reset/%s">`, id)))
		w.Write([]byte(`<input type="submit" value="Reset">`))
		w.Write([]byte(`</form>`))

		w.Write([]byte("<h2>Configuration</h2><table>"))
		w.Write([]byte("<tr><th>Key</th><th>Value</th></tr>"))
		for _, k := range cp.ConfigKeys {
			w.Write([]byte(fmt.Sprintf("<tr><td>%s</td><td>%s</td></tr>", k.Key, *k.Value)))
		}
		w.Write([]byte("</table>"))

		w.Write([]byte("<h2>Connectors</h2><ul>"))
		for cID, c := range cp.Connectors {
			w.Write([]byte(fmt.Sprintf("<li>%d - %s", cID, c.Status)))
			if c.hasTransactionInProgress() {
				w.Write([]byte(fmt.Sprintf(" - Current Transaction: %d", c.CurrentTransaction)))
				if time.Since(c.MeasurementTime) < 30*time.Second {
					w.Write([]byte(fmt.Sprintf(" providing %.1fW @ %.1fV / %.1fA", c.LastMeasurement.ActivePower, c.LastMeasurement.Voltage, c.LastMeasurement.Current)))
				}
			} else {
				w.Write([]byte(fmt.Sprintf(`<form method="post" action="/start/%s/%d">`, id, cID)))
				w.Write([]byte(`<input type="submit" value="Start">`))
				w.Write([]byte(`</form>`))
			}

			w.Write([]byte("</li>"))

		}
		w.Write([]byte("</ul>"))

		w.Write([]byte("<h2>Transactions</h2><table>"))
		w.Write([]byte("<tr><th>ID</th><th>Connector</th><th>Tag</th><th>Start</th><th>End</th><th>Start Meter</th><th>End Meter</th><th></th></tr>"))
		for tID, t := range cp.Transactions {
			w.Write([]byte(fmt.Sprintf("<tr><td>%d</td><td>%d</td><td>%s</td><td>%s</td><td>%s</td><td>%d</td><td>",
				tID, t.ConnectorId, t.IDTag, t.StartTime, t.EndTime, t.StartMeter)))
			if time.Since(t.MeasurementTime) < 30*time.Second {
				w.Write([]byte(fmt.Sprintf("providing %.1fW @ %.1fV / %.1fA", t.LastMeasurement.ActivePower, t.LastMeasurement.Voltage, t.LastMeasurement.Current)))
				w.Write([]byte(fmt.Sprintf(" (total %d W since start)", t.LastMeasurement.MeterReading-int64(t.StartMeter))))
			} else {
				if t.EndMeter > 0 {
					w.Write([]byte(fmt.Sprintf("%d (%d W supplied)", t.EndMeter, t.EndMeter-t.StartMeter)))
				}
			}
			w.Write([]byte("</td><td>"))
			if t.EndTime == nil || t.EndTime.IsZero() {
				w.Write([]byte(fmt.Sprintf(`<form method="post" action="/stop/%s/%d">`, id, tID)))
				w.Write([]byte(`<input type="submit" value="Stop">`))
				w.Write([]byte(`</form>`))
			}
			w.Write([]byte("<td></tr>"))
		}
		w.Write([]byte("</table>"))

	}

	w.Write([]byte("</body></html>"))
}

func Reset(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`POST required`))
		return
	}

	chargePointId := path.Base(r.URL.Path)
	_, ok := handler.chargePoints[chargePointId]
	if !ok {
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte(`Unknown charge point`))
		return
	}

	var ch = make(chan (int), 1)
	cb := func(conf *core.ResetConfirmation, err error) {
		if conf.Status == core.ResetStatusAccepted {
			w.WriteHeader(http.StatusOK)
			w.Write([]byte(`<html><head><meta http-equiv="refresh" content="2; url=/"><title>MG Controller</title></head><body>`))
			w.Write([]byte(`Reset accepted. Redirecting back to main...`))
			w.Write([]byte("</body></html>"))
		} else {
			w.WriteHeader(http.StatusInternalServerError)
			w.Write([]byte(`<html><head><meta http-equiv="refresh" content="2; url=/"><title>MG Controller</title></head><body>`))
			w.Write([]byte(`Reset rejected. Redirecting back to main...`))
			w.Write([]byte("</body></html>"))
		}
		// return from outer fund
		ch <- 1
	}
	err := centralSystem.Reset(chargePointId, cb, core.ResetTypeSoft)
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(fmt.Sprintf(`Reset request failed: %v`, err)))
		return
	}

	<-ch // block until callback runs
}

func WithConnector(connID int) func(*core.RemoteStartTransactionRequest) {
	return func(req *core.RemoteStartTransactionRequest) {
		req.ConnectorId = &connID
	}
}

func StartTransaction(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`POST required`))
		return
	}

	dir, tS := path.Split(r.URL.Path)
	chargePointId := path.Base(dir)
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte(`Unknown charge point`))
		return
	}
	connectorID, err := strconv.Atoi(tS)
	if err != nil {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`Invalid transaction ID`))
		return
	}
	connector, ok := info.Connectors[connectorID]
	if !ok {
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte(`Unknown connector`))
		return
	}

	if connector.hasTransactionInProgress() {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`Connector in use!`))
		return
	}

	var ch = make(chan (int), 1)
	cb := func(conf *core.RemoteStartTransactionConfirmation, err error) {
		if conf.Status == types.RemoteStartStopStatusAccepted {
			w.WriteHeader(http.StatusOK)
			w.Write([]byte(`<html><head><meta http-equiv="refresh" content="2; url=/"><title>MG Controller</title></head><body>`))
			w.Write([]byte(`Start accepted. Redirecting back to main...`))
			w.Write([]byte("</body></html>"))
		} else {
			w.WriteHeader(http.StatusInternalServerError)
			w.Write([]byte(`<html><head><meta http-equiv="refresh" content="2; url=/"><title>MG Controller</title></head><body>`))
			w.Write([]byte(`Start rejected. Redirecting back to main...`))
			w.Write([]byte("</body></html>"))
		}
		// return from outer func
		ch <- 1
	}
	err = centralSystem.RemoteStartTransaction(chargePointId, cb, "1", WithConnector(connectorID))
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(fmt.Sprintf(`Start request failed: %v`, err)))
		return
	}

	<-ch // block until callback runs
}

func StopTransaction(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`POST required`))
		return
	}

	dir, tS := path.Split(r.URL.Path)
	chargePointId := path.Base(dir)
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte(`Unknown charge point`))
		return
	}
	tID, err := strconv.Atoi(tS)
	if err != nil {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`Invalid transaction ID`))
		return
	}
	trans, ok := info.Transactions[tID]
	if !ok {
		w.WriteHeader(http.StatusNotFound)
		w.Write([]byte(`Unknown transaction`))
		return
	}

	if trans.EndTime != nil && !trans.EndTime.IsZero() {
		w.WriteHeader(http.StatusBadRequest)
		w.Write([]byte(`Transaction already ended`))
		return
	}

	var ch = make(chan (int), 1)
	cb := func(conf *core.RemoteStopTransactionConfirmation, err error) {
		if conf.Status == types.RemoteStartStopStatusAccepted {
			w.WriteHeader(http.StatusOK)
			w.Write([]byte(`<html><head><meta http-equiv="refresh" content="2; url=/"><title>MG Controller</title></head><body>`))
			w.Write([]byte(`Stop accepted. Redirecting back to main...`))
			w.Write([]byte("</body></html>"))
		} else {
			w.WriteHeader(http.StatusInternalServerError)
			w.Write([]byte(`<html><head><meta http-equiv="refresh" content="2; url=/"><title>MG Controller</title></head><body>`))
			w.Write([]byte(`Stop rejected. Redirecting back to main...`))
			w.Write([]byte("</body></html>"))
		}
		// return from outer func
		ch <- 1
	}
	err = centralSystem.RemoteStopTransaction(chargePointId, cb, tID)
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		w.Write([]byte(fmt.Sprintf(`Stop request failed: %v`, err)))
		return
	}

	<-ch // block until callback runs
}
