/* Ecomist controller definitions.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#ifndef Ecomist_h
#define Ecomist_h

#include <Arduino.h>

#include "Sensor.h"

// Control the solenoid in an Ecomist E4K dispenser
class Ecomist: public SensorTmpl<Ecomist> {
  public:
    Ecomist(const std::string name_in, const uint8_t pin_in) : Sensor {name_in, pin_in} {} 
    void Setup();
    void Shutdown();
    void Collect();

    static std::string SensorType() { return "Ecomist"; }
  protected:
};

#endif
// vim: set ts=2 sw=2 sts=2 et:
