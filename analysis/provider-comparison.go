// Copyright (C) 2018 Matt Brown. All rights reserved.

// Helper utility to compare power providers
package main

import (
    "bufio"
    "database/sql"
    "fmt"
    "log"
    "math"
    "os"
    "strings"
    "time"
    _ "github.com/mattn/go-sqlite3"
)

const providerSummary = `
SELECT SUM(kWh), SUM(cost) FROM (
    SELECT t.tariff_name,
           SUM(p.wh_in)/1000.0 as kWh,
           SUM(p.wh_in)/1000.0*tr.cent_kwh/100.0 as cost
    FROM power p
        LEFT JOIN tariff t
            ON strftime("%H%M", p.half_hour_ending)=strftime("%H%M", t.half_hour_ending)
        LEFT JOIN tariff_rate tr
            ON t.tariff_name=tr.tariff_name
    WHERE
        t.provider=?
        AND p.half_hour_ending>?
        AND p.half_hour_ending<?
    GROUP BY t.tariff_name
)
`

func GetProviders(db *sql.DB) ([]string) {
    rows, err := db.Query("SELECT DISTINCT provider FROM tariff")
	if err != nil {
		log.Fatal(err)
	}
	defer rows.Close()
    var s []string
	for rows.Next() {
		var name string
		err = rows.Scan(&name)
		if err != nil {
			log.Fatal(err)
		}
        s = append(s, name)
	}
    return s
}

// Run a set of queries for each provider over a specified period.
func QueryPerProvider(start, end time.Time, providers []string, db *sql.DB) {
    kWh := 0.0
    var out []string
    out = append(out, end.Format("2006-01-02"))
    for _, provider := range providers {
        var p_kWh float64
        var p_cost float64
        err := db.QueryRow(providerSummary,
                          provider, start.Format(time.RFC3339),
                          end.Format(time.RFC3339)).Scan(&p_kWh, &p_cost)
        if err != nil {
            log.Fatal(err)
        }
        if kWh == 0 {
            kWh = p_kWh
            out = append(out, fmt.Sprintf("%.2f", kWh))
        } else {
            if math.Abs(kWh - p_kWh) > 0.001 {
                log.Fatal("per-provider kWh mismatch (? previously vs ? for ?)",
                          kWh, p_kWh, provider)
            }
        }
        out = append(out, fmt.Sprintf("%.2f", p_cost))
    }
    fmt.Println(strings.Join(out, ","))
}

func ParseBillDates(filename string) ([]time.Time) {
    fp, err := os.Open(filename)
    if err != nil {
        log.Fatal(err)
    }
    defer fp.Close()
    scanner := bufio.NewScanner(fp)

    var dates []time.Time
    for scanner.Scan() {
        d, err := time.ParseInLocation("2006-01-02", scanner.Text(), time.Local)
        if err != nil {
            log.Fatal(err)
        }
        dates = append(dates, d)
    }
    return dates
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

    providers := GetProviders(db)
    var header []string
    header = append(header, "Bill Date", "kWh")
    for _, name := range providers {
        header = append(header, name)
    }
    fmt.Println(strings.Join(header, ","))


    dates := ParseBillDates(os.Args[1])
    last := dates[0]
    for _, bill := range dates[1:] {
        start := last.Add(time.Hour * 24)
        end := bill.Add((time.Hour * 24)+1)
        QueryPerProvider(start, end, providers, db)
        last = bill
    }
}
