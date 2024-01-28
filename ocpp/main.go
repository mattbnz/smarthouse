package main

import (
	"fmt"
	"net/http"
	"os"
	"strconv"

	"github.com/sirupsen/logrus"

	ocpp16 "github.com/lorenzodonini/ocpp-go/ocpp1.6"
	"github.com/lorenzodonini/ocpp-go/ocpp1.6/core"
	"github.com/lorenzodonini/ocpp-go/ocppj"
	"github.com/lorenzodonini/ocpp-go/ws"
)

const (
	defaultOCPPPort          = 8887
	defaultHTTPPort          = 8080
	defaultHeartbeatInterval = 600
	envVarOCPPPort           = "SERVER_OCPP_PORT"
	envVarHTTPPort           = "SERVER_HTTP_PORT"
	envVarDBPath             = "DB_PATH"
)

var defaultDBPath = os.ExpandEnv("$HOME/.local/share/smarthouse/ocpp.db")
var infoLog *logrus.Logger
var debugLog *logrus.Logger
var centralSystem ocpp16.CentralSystem
var handler *CentralSystemHandler
var store *Store

func setupCentralSystem() ocpp16.CentralSystem {
	return ocpp16.NewCentralSystem(nil, nil)
}

// Requests and stores the configuration when a charge point connects.
func requestConfiguration(chargePointID string, handler *CentralSystemHandler) {
	cb := func(conf *core.GetConfigurationConfirmation, err error) {
		handler.OnConfig(chargePointID, conf, err)
	}
	err := centralSystem.GetConfiguration(chargePointID, cb, []string{})
	if err != nil {
		logDefault(chargePointID, "GetConfiguration").Errorf("couldn't send message: %v", err)
		return
	}
}

// Start function
func main() {
	// Load config from ENV
	var ocppPort = defaultOCPPPort
	port, _ := os.LookupEnv(envVarOCPPPort)
	if p, err := strconv.Atoi(port); err == nil {
		ocppPort = p
	} else {
		infoLog.Printf("no valid %v environment variable found, using default port", envVarOCPPPort)
	}
	var httpPort = defaultHTTPPort
	port, _ = os.LookupEnv(envVarHTTPPort)
	if p, err := strconv.Atoi(port); err == nil {
		ocppPort = p
	} else {
		infoLog.Printf("no valid %v environment variable found, using default port", envVarHTTPPort)
	}
	var dbPath = defaultDBPath
	if p, found := os.LookupEnv(envVarDBPath); found {
		dbPath = p
	}

	centralSystem = setupCentralSystem()

	// Support callbacks for all OCPP 1.6 profiles
	handler = &CentralSystemHandler{chargePoints: map[string]*ChargePointState{}}
	var err error
	var shutdown = make(chan bool)
	store, err = NewStore(dbPath, handler, shutdown)
	if err != nil {
		infoLog.Fatalf("Could not initialise store: %v", err)
	}
	if err := store.Restore(); err != nil {
		infoLog.Fatalf("Failed to restore state: %v", err)
	}
	centralSystem.SetCoreHandler(handler)
	centralSystem.SetLocalAuthListHandler(handler)
	centralSystem.SetFirmwareManagementHandler(handler)
	centralSystem.SetReservationHandler(handler)
	centralSystem.SetRemoteTriggerHandler(handler)
	centralSystem.SetSmartChargingHandler(handler)
	// Add handlers for dis/connection of charge points
	centralSystem.SetNewChargePointHandler(func(chargePoint ocpp16.ChargePointConnection) {
		handler.chargePoints[chargePoint.ID()] = &ChargePointState{Connectors: map[int]*ConnectorInfo{}, Transactions: map[int]*TransactionInfo{}}
		infoLog.WithField("client", chargePoint.ID()).Infof("new charge point connected from %s", chargePoint.RemoteAddr())
		go requestConfiguration(chargePoint.ID(), handler)
	})
	centralSystem.SetChargePointDisconnectedHandler(func(chargePoint ocpp16.ChargePointConnection) {
		infoLog.WithField("client", chargePoint.ID()).Info("charge point disconnected")
		delete(handler.chargePoints, chargePoint.ID())
	})
	ocppj.SetLogger(debugLog.WithField("logger", "ocppj"))
	ws.SetLogger(infoLog.WithField("logger", "websocket"))
	// Run central system
	infoLog.Infof("starting central system on port %v", ocppPort)
	go centralSystem.Start(ocppPort, "/{ws}")
	go store.Run()

	infoLog.Infof("starting http server on port %v", httpPort)
	http.ListenAndServe(fmt.Sprintf(":%d", httpPort), nil)
	select {
	case shutdown <- true:
	default:
	}
}

func init() {
	infoLog = logrus.New()
	infoLog.SetFormatter(&logrus.TextFormatter{FullTimestamp: true})
	infoLog.SetLevel(logrus.InfoLevel)

	debugLog = logrus.New()
	debugLog.SetFormatter(&logrus.TextFormatter{FullTimestamp: true})
	debugLog.SetLevel(logrus.DebugLevel)
}
