package main

import (
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func Test_Store(t *testing.T) {
	testHandler := &CentralSystemHandler{chargePoints: map[string]*ChargePointState{}}
	shutdown := make(chan bool)
	s, err := NewStore(":memory:", testHandler, shutdown)
	require.NoError(t, err)

	s.Save()

	var jB string
	err = s.db.QueryRow("SELECT value FROM state WHERE key='chargePoints' LIMIT 1").Scan(&jB)
	require.NoError(t, err)
	assert.NotEmpty(t, jB)
}

func Test_Store_Background(t *testing.T) {
	old := StoreDumpInterval
	defer func() { StoreDumpInterval = old }()
	StoreDumpInterval = 500 * time.Millisecond

	testHandler := &CentralSystemHandler{chargePoints: map[string]*ChargePointState{}}
	shutdown := make(chan bool)
	s, err := NewStore(":memory:", testHandler, shutdown)
	require.NoError(t, err)
	go s.Run()

	time.Sleep(1 * time.Second)
	select {
	case shutdown <- true:
	default:
	}

	var jB string
	err = s.db.QueryRow("SELECT value FROM state WHERE key='chargePoints' LIMIT 1").Scan(&jB)
	require.NoError(t, err)
	assert.NotEmpty(t, jB)
}
