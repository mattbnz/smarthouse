/* Water Flow Sensor definitions.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#include <Arduino.h>
#include <FunctionalInterrupt.h>

#include "Sensor.h"
#include "FlowSensor.h"

void IRAM_ATTR FlowSensor::handleInterrupt() {
  pulses++;
}

void FlowSensor::Setup() {
  pulses = 0;
  flowRate = 0.0;
  flowMilliLitres = 0;
  totalMilliLitres = 0;
  lastTime = millis();

  pinMode(pin, INPUT);
  interruptPtr= std::bind( &FlowSensor::handleInterrupt, this);
  attachInterrupt(digitalPinToInterrupt(pin), interruptPtr, RISING);
}

void FlowSensor::Shutdown() {
  detachInterrupt(digitalPinToInterrupt(pin));
}

void FlowSensor::Collect() {
  // Disable interrupts while we read and reset the counter
  detachInterrupt(digitalPinToInterrupt(pin));

  unsigned long now = millis();
  byte p = pulses;
  pulses = 0;

  // Re-enable
  attachInterrupt(digitalPinToInterrupt(pin), interruptPtr, RISING);

  // Now calculate the flow rate, etc.
  if (p > 0) {
    flowRate = pulseToL(p); // result is L/min, instantaneous
    flowRate *= ML_IN_LITRE; // convert to ML
  } else {
    flowRate = 0;
  }

  // divide by fraction of minute that actually passed, to get volume.
  unsigned long elapsed_ms = now - lastTime;
  flowMilliLitres = flowRate / (MS_IN_MIN/elapsed_ms);
  // and update the cumulative counter
  totalMilliLitres += flowMilliLitres;
  // store the time of the current reading for use next time.
  lastTime = now;
}

String FlowSensor::Describe() {
  String base = Sensor::Describe();
  char message[240];
  sprintf(message, " => mL_per_min:%f, flow_mL:%d, total_mL:%ld, pulses:%d",
          flowRate, flowMilliLitres, totalMilliLitres, pulses);
  return base + String(message);
}

String FlowSensor::JSON() {
  char message[240];
  sprintf(message, "{\"mL_per_min\":%f,\"flow_mL\":%d, \"total_mL\":%ld}",
          flowRate, flowMilliLitres, totalMilliLitres);
  return String(message);
}

String FlowSensor::MQTTSuffix() {
  return String("/flow-sensor/") + String(pin, 10);
}

float FlowSensor::pulseToL(byte p) {
  return p;
}

float YFB10FlowSensor::pulseToL(byte p) {
  // Datasheet details
  //   pulse characteristic (6*Q-8) Q=L/Min±5%
  //   aka pulses=6*L_per_min-8;
  //  solved for L_per_min = 1/6*pulses + 4/3
  //  except, if we didn't see any pulses, we don't add the constant factor
  //  because it's not plausible that we're actually consistently consuming
  // 1.3L/min of water, which is what that would imply.
  return (1.0/6.0)*p + (4.0/3.0);
}

float FS400AFlowSensor::pulseToL(byte p) {
  // Datasheet details
  //  F = 4.8 * Q (L / Min) error: ± 2%
  //  constant frequency calculation = 4.8 * (L / min) * Time (seconds)
  //  1-60L / min
  return (1/4.8)*p;
}

// vim: set ts=2 sw=2 sts=2 et:
