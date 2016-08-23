// Jeenode Meter Reader.
// 
// Copyright (C) 2012 - Matt Brown
//
// All rights reserved.

#define D_PIN 10

void setup() {
  pinMode(D_PIN, OUTPUT);
}

void loop() {
  digitalWrite(D_PIN, HIGH);
  delay(2000);
  digitalWrite(D_PIN, LOW);
  delay(2000);
}

// Vim modeline
// vim: set ts=2 sw=2 sts=2 et:
