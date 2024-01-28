package main

import (
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"time"

	_ "github.com/mattn/go-sqlite3"
)

var StoreDumpInterval = 30 * time.Second

type Store struct {
	db       *sql.DB
	shutdown chan bool

	handler *CentralSystemHandler
}

func NewStore(path string, handler *CentralSystemHandler, shutdown chan bool) (store *Store, err error) {
	store = &Store{handler: handler, shutdown: shutdown}
	store.db, err = sql.Open("sqlite3", "file:"+path+"?_journal=wal")
	if err != nil {
		return
	}
	err = store.Init()
	if err != nil {
		return
	}
	return
}

// Init ensures the necessary tables are present.
func (s *Store) Init() error {
	var cnt int
	err := s.db.QueryRow("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = 'state'").Scan(&cnt)
	if err != nil {
		return err
	}
	if cnt != 1 {
		if err := s.createTable(); err != nil {
			return err
		}
	}
	err = s.db.QueryRow("SELECT COUNT(*) FROM state").Scan(&cnt)
	if err != nil {
		return err
	}
	infoLog.Infof("Store initialized with %d state records available", cnt)
	return nil
}

func (s *Store) createTable() (err error) {
	_, err = s.db.Exec("CREATE TABLE state (key text primary key, value text);")
	if err != nil {
		infoLog.Infof("Created state table!")
	}
	return
}

// Run dumps the handlers state to the DB every interval. Expected to be run as a goroutine.
func (s *Store) Run() {
	ticker := time.NewTicker(StoreDumpInterval)
	debugLog.Infof("Entering background store loop. Interval " + StoreDumpInterval.String())
	for {
		select {
		case <-s.shutdown:
			fmt.Println("Shutting down store!")
			ticker.Stop()
			return
		case <-ticker.C:
			s.Save()
		}
	}
}

// Save dumps the current state of the handler to the DB.
func (s *Store) Save() {
	jD, err := json.Marshal(s.handler.chargePoints)
	if err != nil {
		infoLog.Errorf("Could not marshal handler.chargePoints to save! %v", err)
		return
	}
	_, err = s.db.Exec("insert or replace into state(key, value) values('chargePoints', ?)", jD)
	if err != nil {
		infoLog.Errorf("Failed to save state: %v", err)
		return
	}
	debugLog.Infof("Saved handler state")
}

// Restore returns the cached state for the handler
func (s *Store) Restore() (err error) {
	var jB []byte
	err = s.db.QueryRow("SELECT value FROM state WHERE key = 'chargePoints' LIMIT 1").Scan(&jB)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			err = nil // nothing to restore
			return
		}
		return
	}
	cp := map[string]*ChargePointState{}
	err = json.Unmarshal(jB, &cp)
	if err != nil {
		return
	}
	s.handler.chargePoints = cp
	for _, p := range s.handler.chargePoints {
		for _, t := range p.Transactions {
			if t.ID >= nextTransactionId {
				nextTransactionId = t.ID + 1
			}
		}
	}
	infoLog.Infof("Restored %d charge points from state", len(s.handler.chargePoints))
	return nil
}
