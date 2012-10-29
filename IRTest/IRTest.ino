// Jeenode Meter Reader.
// 
// Copyright (C) 2012 - Matt Brown
//
// All rights reserved.

#define D_PIN 4
#define A_PIN 0

void setup() {
  Serial.begin(57600);
}

void loop() {
  int state = digitalRead(D_PIN);
  int anlg = analogRead(A_PIN);
  Serial.print("Digital=");
  Serial.print(state);
  Serial.print(". Analog=");
  Serial.print(anlg);
  Serial.println(".");
  delay(500);
}

// Vim modeline
// vim: set ts=2 sw=2 sts=2 et:
