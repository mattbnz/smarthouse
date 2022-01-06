/* Ecomist controller
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#include <Arduino.h>

#include "Sensor.h"
#include "Ecomist.h"
#include "util.h"

void Ecomist::Setup() {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void Ecomist::Shutdown() {
  digitalWrite(pin, LOW);
}

void Ecomist::Collect() {
  _D("Spritz");
  digitalWrite(pin, HIGH);
  delay(5);
  digitalWrite(pin, LOW);
}
// vim: set ts=2 sw=2 sts=2 et:
