package main

import (
	"fmt"
	"time"

	"github.com/sirupsen/logrus"

	"github.com/lorenzodonini/ocpp-go/ocpp1.6/core"
	"github.com/lorenzodonini/ocpp-go/ocpp1.6/firmware"
	"github.com/lorenzodonini/ocpp-go/ocpp1.6/types"
)

var (
	nextTransactionId = 0
)

// TransactionInfo contains info about a transaction
type TransactionInfo struct {
	ID          int
	StartTime   *types.DateTime
	EndTime     *types.DateTime
	StartMeter  int
	EndMeter    int
	ConnectorId int
	IDTag       string
}

func (ti *TransactionInfo) hasTransactionEnded() bool {
	return ti.EndTime != nil && !ti.EndTime.IsZero()
}

// ConnectorInfo contains status and ongoing transaction ID for a connector
type ConnectorInfo struct {
	Status             core.ChargePointStatus
	CurrentTransaction int
}

func (ci *ConnectorInfo) hasTransactionInProgress() bool {
	return ci.CurrentTransaction >= 0
}

// ChargePointState contains some simple state for a connected charge point
type ChargePointState struct {
	BootData          core.BootNotificationRequest
	Status            core.ChargePointStatus
	DiagnosticsStatus firmware.DiagnosticsStatus
	FirmwareStatus    firmware.FirmwareStatus
	Connectors        map[int]*ConnectorInfo // No assumptions about the # of connectors
	Transactions      map[int]*TransactionInfo
	ErrorCode         core.ChargePointErrorCode
	ConfigKeys        []core.ConfigurationKey
}

func (cps *ChargePointState) getConnector(id int) *ConnectorInfo {
	ci, ok := cps.Connectors[id]
	if !ok {
		ci = &ConnectorInfo{CurrentTransaction: -1}
		cps.Connectors[id] = ci
	}
	return ci
}

// CentralSystemHandler contains some simple state that a central system may want to keep.
// In production this will typically be replaced by database/API calls.
type CentralSystemHandler struct {
	chargePoints map[string]*ChargePointState
}

var _ core.CentralSystemHandler = &CentralSystemHandler{}

// ------------- Core profile callbacks -------------

func (handler *CentralSystemHandler) OnAuthorize(chargePointId string, request *core.AuthorizeRequest) (confirmation *core.AuthorizeConfirmation, err error) {
	logDefault(chargePointId, request.GetFeatureName()).Infof("client authorized")
	return core.NewAuthorizationConfirmation(types.NewIdTagInfo(types.AuthorizationStatusAccepted)), nil
}

func (handler *CentralSystemHandler) OnBootNotification(chargePointId string, request *core.BootNotificationRequest) (confirmation *core.BootNotificationConfirmation, err error) {
	logDefault(chargePointId, request.GetFeatureName()).Infof("boot confirmed")
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		logDefault(chargePointId, request.GetFeatureName()).Warnf("Boot notification from unknown charge point")
	} else {
		info.BootData = *request
	}
	return core.NewBootNotificationConfirmation(types.NewDateTime(time.Now()), defaultHeartbeatInterval, core.RegistrationStatusAccepted), nil
}

func (handler *CentralSystemHandler) OnDataTransfer(chargePointId string, request *core.DataTransferRequest) (confirmation *core.DataTransferConfirmation, err error) {
	logDefault(chargePointId, request.GetFeatureName()).Infof("received data %d", request.Data)
	return core.NewDataTransferConfirmation(core.DataTransferStatusAccepted), nil
}

func (handler *CentralSystemHandler) OnHeartbeat(chargePointId string, request *core.HeartbeatRequest) (confirmation *core.HeartbeatConfirmation, err error) {
	logDefault(chargePointId, request.GetFeatureName()).Infof("heartbeat handled")
	return core.NewHeartbeatConfirmation(types.NewDateTime(time.Now())), nil
}

func (handler *CentralSystemHandler) OnMeterValues(chargePointId string, request *core.MeterValuesRequest) (confirmation *core.MeterValuesConfirmation, err error) {
	logDefault(chargePointId, request.GetFeatureName()).Infof("received meter values for connector %v. Meter values:\n", request.ConnectorId)
	for _, mv := range request.MeterValue {
		logDefault(chargePointId, request.GetFeatureName()).Printf("%v", mv)
	}
	return core.NewMeterValuesConfirmation(), nil
}

func (handler *CentralSystemHandler) OnStatusNotification(chargePointId string, request *core.StatusNotificationRequest) (confirmation *core.StatusNotificationConfirmation, err error) {
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		return nil, fmt.Errorf("unknown charge point %v", chargePointId)
	}
	info.ErrorCode = request.ErrorCode
	if request.ConnectorId > 0 {
		connectorInfo := info.getConnector(request.ConnectorId)
		connectorInfo.Status = request.Status
		logDefault(chargePointId, request.GetFeatureName()).Infof("connector %v updated status to %v", request.ConnectorId, request.Status)
	} else {
		info.Status = request.Status
		logDefault(chargePointId, request.GetFeatureName()).Infof("all connectors updated status to %v", request.Status)
	}
	return core.NewStatusNotificationConfirmation(), nil
}

func (handler *CentralSystemHandler) OnStartTransaction(chargePointId string, request *core.StartTransactionRequest) (confirmation *core.StartTransactionConfirmation, err error) {
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		return nil, fmt.Errorf("unknown charge point %v", chargePointId)
	}
	connector := info.getConnector(request.ConnectorId)
	if connector.CurrentTransaction >= 0 {
		return nil, fmt.Errorf("connector %v is currently busy with another transaction", request.ConnectorId)
	}
	transaction := &TransactionInfo{}
	transaction.IDTag = request.IdTag
	transaction.ConnectorId = request.ConnectorId
	transaction.StartMeter = request.MeterStart
	transaction.StartTime = request.Timestamp
	transaction.ID = nextTransactionId
	nextTransactionId += 1
	connector.CurrentTransaction = transaction.ID
	info.Transactions[transaction.ID] = transaction
	//TODO: check billable clients
	logDefault(chargePointId, request.GetFeatureName()).Infof("started transaction %v for connector %v", transaction.ID, transaction.ConnectorId)
	return core.NewStartTransactionConfirmation(types.NewIdTagInfo(types.AuthorizationStatusAccepted), transaction.ID), nil
}

func (handler *CentralSystemHandler) OnStopTransaction(chargePointId string, request *core.StopTransactionRequest) (confirmation *core.StopTransactionConfirmation, err error) {
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		return nil, fmt.Errorf("unknown charge point %v", chargePointId)
	}
	transaction, ok := info.Transactions[request.TransactionId]
	if ok {
		connector := info.getConnector(transaction.ConnectorId)
		connector.CurrentTransaction = -1
		transaction.EndTime = request.Timestamp
		transaction.EndMeter = request.MeterStop
		//TODO: bill charging period to client
	}
	logDefault(chargePointId, request.GetFeatureName()).Infof("stopped transaction %v - %v", request.TransactionId, request.Reason)
	for _, mv := range request.TransactionData {
		logDefault(chargePointId, request.GetFeatureName()).Printf("%v", mv)
	}
	return core.NewStopTransactionConfirmation(), nil
}

// ------------- Firmware management profile callbacks -------------

func (handler *CentralSystemHandler) OnDiagnosticsStatusNotification(chargePointId string, request *firmware.DiagnosticsStatusNotificationRequest) (confirmation *firmware.DiagnosticsStatusNotificationConfirmation, err error) {
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		return nil, fmt.Errorf("unknown charge point %v", chargePointId)
	}
	info.DiagnosticsStatus = request.Status
	logDefault(chargePointId, request.GetFeatureName()).Infof("updated diagnostics status to %v", request.Status)
	return firmware.NewDiagnosticsStatusNotificationConfirmation(), nil
}

func (handler *CentralSystemHandler) OnFirmwareStatusNotification(chargePointId string, request *firmware.FirmwareStatusNotificationRequest) (confirmation *firmware.FirmwareStatusNotificationConfirmation, err error) {
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		return nil, fmt.Errorf("unknown charge point %v", chargePointId)
	}
	info.FirmwareStatus = request.Status
	logDefault(chargePointId, request.GetFeatureName()).Infof("updated firmware status to %v", request.Status)
	return &firmware.FirmwareStatusNotificationConfirmation{}, nil
}

func (handler *CentralSystemHandler) OnConfig(chargePointId string, conf *core.GetConfigurationConfirmation, err error) {
	if err != nil {
		logDefault(chargePointId, "GetConfiguration").Errorf("request returned error: %v", err)
		return
	}
	info, ok := handler.chargePoints[chargePointId]
	if !ok {
		logDefault(chargePointId, "GetConfiguration").Error("unknown charge point")
		return
	}
	info.ConfigKeys = conf.ConfigurationKey
	for _, k := range conf.UnknownKey {
		logDefault(chargePointId, "Configuration").Warnf("Unknown configuration key: %s", k)
	}
}

// No callbacks for Local Auth management, Reservation, Remote trigger or Smart Charging profile on central system

// Utility functions

func logDefault(chargePointId string, feature string) *logrus.Entry {
	return infoLog.WithFields(logrus.Fields{"client": chargePointId, "message": feature})
}
