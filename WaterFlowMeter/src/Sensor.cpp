/* Base sensor class.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#include "Sensor.h"

Sensor::Sensor(const std::string name_in, const uint8_t pin_in) {
  name = name_in;
  pin = pin_in;
}

String Sensor::Describe() {
  return String(instance.c_str()) + "for " + name.c_str() + " on " + String(pin, 10);
}

String Sensor::JSON() {
  return "";
}

String Sensor::MQTTSuffix() {
  return "";
}

//template <typename T> std::unique_ptr<Sensor> MakeSensor(const uint8_t pin_in) {
//  return std::unique_ptr<Sensor>{new T{pin_in}};
//}
// vim: set ts=2 sw=2 sts=2 et:
