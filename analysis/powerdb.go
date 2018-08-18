// Copyright (C) 2018 Matt Brown. All rights reserved.

// Translate MeterReader log files into a SQLite database.
// 
// The MeterReader log files saved by mlogger are aggregated into 30-min
// aligned records and recorded into a SQLite database so that they can be more
// easily analyzed for cost comparison, etc.
package main

import (
    "bufio"
    "database/sql"
    "fmt"
    "log"
    "os"
    "path/filepath"
    "strconv"
    "strings"
    "time"
    _ "github.com/mattn/go-sqlite3"
)

const tsFormat = "2006-01-02 15:04:"

// Return the most recent period which the DB has data for.
func GetLastUpdate(db *sql.DB) (time.Time) {
    rows, err := db.Query(`SELECT half_hour_ending FROM power
                           ORDER BY half_hour_ending DESC LIMIT 1`)
	if err != nil {
		log.Fatal(err)
	}
	defer rows.Close()
	for rows.Next() {
		var half_hour_ending string
		err = rows.Scan(&half_hour_ending)
		if err != nil {
			log.Fatal(err)
		}
        ts, err := time.Parse(time.RFC3339, half_hour_ending)
        if err != nil {
            log.Fatal(err)
        }
        return ts
	}
    // No rows, return sentinel (first date logs are recorded for).
    return time.Date(2017, 6, 8, 20, 0, 0, 0, time.Local)
}

// Returns the last full period before the specified time.
func GetLastFullPeriod(ts time.Time) (time.Time) {
    min := 0
    if ts.Minute() >= 30 {
        min = 30
    }
    return time.Date(ts.Year(), ts.Month(), ts.Day(),
                     ts.Hour(), min, 0, 0, time.Local)
}

// Returns the period before the specified time.
func GetPrevPeriod(period time.Time) (time.Time) {
    // Use GetLastFullPeriod to align the time we were passed
    period = GetLastFullPeriod(period)
    return period.Add(-(time.Minute * 30))
}

// Returns the period after the specified time.
func GetNextPeriod(period time.Time) (time.Time) {
    // Use GetLastFullPeriod to align the time we were passed
    period = GetLastFullPeriod(period)
    return period.Add(time.Minute * 30)
}

// Returns the path to the file expected to contain logs for a period.
func GetPeriodFilename(period time.Time) (string) {
    // Files are named after UTC
    utc := period.UTC()
    // Our periods are named based on when they end, e.g. the period for the
    // 2nd 30 minutes of an hour (e.g. 00:30-00:59) is named by the timestamp
    // 01:00. So if we've been passed a period with minutes==0, we actually
    // need to subtract 1 minute to get the right filename to look into.
    if utc.Minute() == 0 {
        utc = utc.Add(-(time.Minute))
    }
    return filepath.Join("/tmp/logs", utc.Format("2006010215.log"))
}

// Creates the DB record for the period by scanning the logs and aggregating.
func InsertPeriodToDB(period time.Time, db *sql.DB) {
    filename := GetPeriodFilename(period)

    startTime := GetPrevPeriod(period)
    endTime := period.Add(-1) // to get >= out of time.After

    fd, err := os.Open(filename)
    wh := 0
    if err != nil {
        fmt.Println("File not found!")
    } else {
        defer fd.Close()
        scanner := bufio.NewScanner(fd)

        for scanner.Scan() {
            parts := strings.Split(scanner.Text(), " ")
            if len(parts) < 2 {
                continue
            }
            // extract timestamp
            t, err := strconv.ParseInt(parts[0], 10, 64)
            if err != nil {
                continue
            }
            ts := time.Unix(t, 0)
            if (ts.Before(startTime) || ts.After(endTime)) {
                continue
            }
            // log format changed mid-2017 to contain more fields, so the
            // tag we're looking for may be at idx 1 or 2, depending on
            // how many fields are in the line in total.
            idx := 1
            if len(parts) >= 4 {
                idx += 1
            }
            if (parts[idx] == "PULSE") {
                wh += 1
            }
        }
    }

    fmt.Println(period.Format(tsFormat), fmt.Sprintf("%d", wh))
    _, err = db.Exec("INSERT INTO power (half_hour_ending, wh_in) VALUES(?, ?)",
                     period.Format(time.RFC3339), wh)
	if err != nil {
		log.Fatal(err)
	}
}

func main() {
    db, err := sql.Open("sqlite3", "/tmp/test.db")
    if err != nil {
        log.Fatal(err)
    }
    defer db.Close()
    if err := db.Ping(); err != nil {
        log.Fatal(err)
    }

    dbPeriod := GetLastUpdate(db)
    fmt.Println("Last DB Update: ", dbPeriod.Format(tsFormat))
    nextPeriod := GetNextPeriod(dbPeriod)
    fmt.Println("Next Period: ", nextPeriod.Format(tsFormat))
    previousPeriod := GetLastFullPeriod(time.Now())
    fmt.Println("Most Recent Period: ", previousPeriod.Format(tsFormat))

    d := nextPeriod
    for d.Before(previousPeriod) {
        InsertPeriodToDB(d, db)
        d = GetNextPeriod(d)
    }
}
