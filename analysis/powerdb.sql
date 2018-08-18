-- Copyright (C) 2018 Matt Brown. All rights reserved.
--
-- SQLite DB schema to accompany powerdb.go

CREATE TABLE power (
    half_hour_ending PRIMARY KEY,
    wh_in INTEGER,
    wh_out INTEGER,
    wh_gen INTEGER
);

CREATE TABLE tariff (
    provider STRING,
    half_hour_ending INTEGER,
    export BOOL,
    tariff_name STRING,
    PRIMARY KEY (provider, half_hour_ending, export)
);

CREATE TABLE tariff_rate (
    tariff_name PRIMARY KEY,
    cent_kwh NUMERIC
);
