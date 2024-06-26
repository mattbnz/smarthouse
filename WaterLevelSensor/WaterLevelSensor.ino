// Water Level Sensor
// 
// Copyright (C) 2014 - Matt Brown
//
// All rights reserved.
//
// Uses an HC-SR04 ultrasonic sensor to measure the height of the water level
// in a water tank.
//
// Depends on the HC-SR04 Ultrasonic library from
// http://freecode.com/projects/hc-sr04-ultrasonic-arduino-library
//
#define __AVR_LIBC_DEPRECATED_ENABLE__ 1
#include <Ultrasonic.h>

#define TRIGGER_PIN 12     // Pin connected to the trigger
#define ECHO_PIN 13        // Pin connected to the echo
#define N_SAMPLES 5        // How many measurements to use each time.
#define BAD_MEASUREMENT 0  // Sentinel return value.

#define MAX_DEV 0.50       // Maximum stddev in cm of an allowable measurement.
#define THRESH 0.02
#define UPDATE_MS 60000

Ultrasonic ultrasonic(TRIGGER_PIN, ECHO_PIN);
unsigned long last_measurement = 0;

void setup() {
  Serial.begin(57600);
  ultrasonic.setDivisor(34.6, Ultrasonic::CM);
  last_measurement = 0;
}

float takeMeasurement() {
  float measures[N_SAMPLES];
  float sum = 0.0;
  for (int i=0; i<N_SAMPLES; ++i) {
    measures[i] = ultrasonic.convert((float)ultrasonic.timing(), Ultrasonic::CM);
    sum += measures[i];
    delay(100);
  }
  float mean = sum / N_SAMPLES;
  sum = 0.0;
  for (int i=0; i<N_SAMPLES; ++i) {
    float tmp = measures[i] - mean;
    sum += (tmp * tmp);
  }
  float stddev = sqrt(sum / (N_SAMPLES - 1));
  float thresh = mean * THRESH;
  if (stddev <= thresh) {
    return mean;
  } else {
    Serial.print("Could not get valid measurement this time! ");
    Serial.print("cm=");
    Serial.print(mean);
    Serial.print(", stdDev=");
    Serial.print(stddev);
    Serial.print(", thresh=");
    Serial.println(thresh);
    return BAD_MEASUREMENT;
  }
}
   
void loop() {
  unsigned long now = millis();
  if (last_measurement == 0 || (unsigned long)(now - last_measurement) >= UPDATE_MS) {
    float cm = takeMeasurement();
    if (cm != BAD_MEASUREMENT) {
      Serial.println(cm);
    }
    last_measurement = now;
  } else {
    delay(1000);
  }
}

// Vim modeline
// vim: set ts=2 sw=2 sts=2 et:


