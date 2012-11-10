// Jeenode Temperature Sensor.
// 
// Copyright (C) 2012 - Matt Brown
//
// All rights reserved.
//
// Regularly reads a temperature sensor and reports measurements.
//
// Based on the radioBlip2 example from Jeelib, 2012-05-09 <jc@wippler.nl>.
#include <JeeLib.h>
#include <avr/sleep.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define BUS_PIN 6     // Pin connected to one-wire bus.

#define NODE_ID 3
#define NODE_GROUP 5
#define SEND_MODE 2   // set to 3 if fuses are e=06/h=DE/l=CE, else set to 2

#define REPORT_FREQ 300000 // Time between reports in ms.

volatile bool adcDone;
volatile unsigned long lastReport;

struct {
  long ping;  // 32-bit counter
  byte id;    // identity, should be different for each node
  byte vcc;  //  VCC before transmit, 1.0V = 0 .. 6.0V = 250
  float temp;
} payload;

// for low-noise/-power ADC readouts, we'll use ADC completion interrupts
ISR(ADC_vect) { adcDone = true; }

// this must be defined since we're using the watchdog for low-power waiting
ISR(WDT_vect) { Sleepy::watchdogEvent(); }

OneWire oneWire(BUS_PIN);
DallasTemperature sensors(&oneWire);

static byte vccRead (byte count =4) {
  set_sleep_mode(SLEEP_MODE_ADC);
  ADMUX = bit(REFS0) | 14; // use VCC as AREF and internal bandgap as input
  bitSet(ADCSRA, ADIE);
  while (count-- > 0) {
    adcDone = false;
    while (!adcDone)
      sleep_mode();
  }
  bitClear(ADCSRA, ADIE);  
  // convert ADC readings to fit in one byte, i.e. 20 mV steps:
  //  1.0V = 0, 1.8V = 40, 3.3V = 115, 5.0V = 200, 6.0V = 250
  return (55U * 1024U) / (ADC + 1) - 50;
}

static float readTemp() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}

void setup() {
  cli();
  CLKPR = bit(CLKPCE);
#if defined(__AVR_ATtiny84__)
  CLKPR = 0; // div 1, i.e. speed up to 8 MHz
#else
  CLKPR = 1; // div 2, i.e. slow down to 8 MHz
#endif
  sei();
  rf12_initialize(NODE_ID, RF12_868MHZ, NODE_GROUP);
  rf12_control(0xC040); // set low-battery level to 2.2V i.s.o. 3.1V
  rf12_sleep(RF12_SLEEP);

  sensors.begin();
  for (int i=0; i<sensors.getDeviceCount(); i++) {
    DeviceAddress tmp;
    sensors.getAddress(tmp, i);  
    sensors.setResolution(tmp, 12);  // We like our resolution.
  }

  payload.id = NODE_ID;
  lastReport = 0;
}

static byte sendPayload () {
  ++payload.ping;

  rf12_sleep(RF12_WAKEUP);
  while (!rf12_canSend())
    rf12_recvDone();
  rf12_sendStart(0, &payload, sizeof payload);
  rf12_sendWait(SEND_MODE);
  rf12_sleep(RF12_SLEEP);
}

void loop() {
  unsigned long now = millis();
  if (lastReport == 0 || (now - lastReport) > REPORT_FREQ) {
    payload.vcc = vccRead();
    payload.temp = readTemp();
    sendPayload();
    lastReport = now;
  }
  Sleepy::loseSomeTime(60000);
}

// Vim modeline
// vim: set ts=2 sw=2 sts=2 et:
