-- Copyright (C) 2018 Matt Brown. All rights reserved.
--
-- Seed data for various power provider tariffs needed for the analysis.

REPLACE INTO tariff_rate (tariff_name, cent_kwh) VALUES
-- Contact
('contact_anytime', '30.00'),
-- Ecotricity Low Solar
('eco_solar_early_am', '11.33'),
('eco_solar_am', 12.90),
('eco_solar_am_peak', 28.07),
('eco_solar_day', 22.79),
('eco_solar_pm_peak', 27.54),
('eco_solar_pm_shoulder', 21.00),
('eco_solar_evening', 13.08),
-- Ecotricity Eco Saver
('eco_saver_early_am', '8.96'),
('eco_saver_am', 9.97),
('eco_saver_am_peak', 26.04),
('eco_saver_day', 20.76),
('eco_saver_pm_peak', 25.56),
('eco_saver_pm_shoulder', 18.46),
('eco_saver_evening', 10.54)

;

REPLACE INTO tariff (provider, half_hour_ending, export, tariff_name) VALUES
-- Contact
('contact', '2017-06-08T00:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T00:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T01:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T01:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T02:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T02:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T03:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T03:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T04:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T04:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T05:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T05:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T06:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T06:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T07:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T07:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T08:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T08:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T09:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T09:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T10:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T10:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T11:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T11:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T12:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T12:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T13:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T13:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T14:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T14:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T15:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T15:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T16:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T16:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T17:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T17:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T18:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T18:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T19:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T19:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T20:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T20:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T21:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T21:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T22:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T22:30:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T23:00:00+12:00', 0, 'contact_anytime'),
('contact', '2017-06-08T23:30:00+12:00', 0, 'contact_anytime'),
-- Ecotricity Solar Low
('ecotricity solar low', '2017-06-08T00:00:00+12:00', 0, 'eco_solar_evening'),
('ecotricity solar low', '2017-06-08T00:30:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T01:00:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T01:30:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T02:00:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T02:30:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T03:00:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T03:30:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T04:00:00+12:00', 0, 'eco_solar_early_am'),
('ecotricity solar low', '2017-06-08T04:30:00+12:00', 0, 'eco_solar_am'),
('ecotricity solar low', '2017-06-08T05:00:00+12:00', 0, 'eco_solar_am'),
('ecotricity solar low', '2017-06-08T05:30:00+12:00', 0, 'eco_solar_am'),
('ecotricity solar low', '2017-06-08T06:00:00+12:00', 0, 'eco_solar_am'),
('ecotricity solar low', '2017-06-08T06:30:00+12:00', 0, 'eco_solar_am'),
('ecotricity solar low', '2017-06-08T07:00:00+12:00', 0, 'eco_solar_am'),
('ecotricity solar low', '2017-06-08T07:30:00+12:00', 0, 'eco_solar_am_peak'),
('ecotricity solar low', '2017-06-08T08:00:00+12:00', 0, 'eco_solar_am_peak'),
('ecotricity solar low', '2017-06-08T08:30:00+12:00', 0, 'eco_solar_am_peak'),
('ecotricity solar low', '2017-06-08T09:00:00+12:00', 0, 'eco_solar_am_peak'),
('ecotricity solar low', '2017-06-08T09:30:00+12:00', 0, 'eco_solar_am_peak'),
('ecotricity solar low', '2017-06-08T10:00:00+12:00', 0, 'eco_solar_am_peak'),
('ecotricity solar low', '2017-06-08T10:30:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T11:00:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T11:30:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T12:00:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T12:30:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T13:00:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T13:30:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T14:00:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T14:30:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T15:00:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T15:30:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T16:00:00+12:00', 0, 'eco_solar_day'),
('ecotricity solar low', '2017-06-08T16:30:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T17:00:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T17:30:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T18:00:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T18:30:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T19:00:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T19:30:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T20:00:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T20:30:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T21:00:00+12:00', 0, 'eco_solar_pm_peak'),
('ecotricity solar low', '2017-06-08T21:30:00+12:00', 0, 'eco_solar_pm_shoulder'),
('ecotricity solar low', '2017-06-08T22:00:00+12:00', 0, 'eco_solar_pm_shoulder'),
('ecotricity solar low', '2017-06-08T22:30:00+12:00', 0, 'eco_solar_pm_shoulder'),
('ecotricity solar low', '2017-06-08T23:00:00+12:00', 0, 'eco_solar_pm_shoulder'),
('ecotricity solar low', '2017-06-08T23:30:00+12:00', 0, 'eco_solar_evening'),
-- Ecotricity Saver
('ecotricity saver', '2017-06-08T00:00:00+12:00', 0, 'eco_saver_evening'),
('ecotricity saver', '2017-06-08T00:30:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T01:00:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T01:30:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T02:00:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T02:30:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T03:00:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T03:30:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T04:00:00+12:00', 0, 'eco_saver_early_am'),
('ecotricity saver', '2017-06-08T04:30:00+12:00', 0, 'eco_saver_am'),
('ecotricity saver', '2017-06-08T05:00:00+12:00', 0, 'eco_saver_am'),
('ecotricity saver', '2017-06-08T05:30:00+12:00', 0, 'eco_saver_am'),
('ecotricity saver', '2017-06-08T06:00:00+12:00', 0, 'eco_saver_am'),
('ecotricity saver', '2017-06-08T06:30:00+12:00', 0, 'eco_saver_am'),
('ecotricity saver', '2017-06-08T07:00:00+12:00', 0, 'eco_saver_am'),
('ecotricity saver', '2017-06-08T07:30:00+12:00', 0, 'eco_saver_am_peak'),
('ecotricity saver', '2017-06-08T08:00:00+12:00', 0, 'eco_saver_am_peak'),
('ecotricity saver', '2017-06-08T08:30:00+12:00', 0, 'eco_saver_am_peak'),
('ecotricity saver', '2017-06-08T09:00:00+12:00', 0, 'eco_saver_am_peak'),
('ecotricity saver', '2017-06-08T09:30:00+12:00', 0, 'eco_saver_am_peak'),
('ecotricity saver', '2017-06-08T10:00:00+12:00', 0, 'eco_saver_am_peak'),
('ecotricity saver', '2017-06-08T10:30:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T11:00:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T11:30:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T12:00:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T12:30:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T13:00:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T13:30:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T14:00:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T14:30:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T15:00:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T15:30:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T16:00:00+12:00', 0, 'eco_saver_day'),
('ecotricity saver', '2017-06-08T16:30:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T17:00:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T17:30:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T18:00:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T18:30:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T19:00:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T19:30:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T20:00:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T20:30:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T21:00:00+12:00', 0, 'eco_saver_pm_peak'),
('ecotricity saver', '2017-06-08T21:30:00+12:00', 0, 'eco_saver_pm_shoulder'),
('ecotricity saver', '2017-06-08T22:00:00+12:00', 0, 'eco_saver_pm_shoulder'),
('ecotricity saver', '2017-06-08T22:30:00+12:00', 0, 'eco_saver_pm_shoulder'),
('ecotricity saver', '2017-06-08T23:00:00+12:00', 0, 'eco_saver_pm_shoulder'),
('ecotricity saver', '2017-06-08T23:30:00+12:00', 0, 'eco_saver_evening')

;

