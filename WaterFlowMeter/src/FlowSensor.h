/* Water Flow Sensor definitions.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#ifndef FlowSensor_h
#define FlowSensor_h

#include <Arduino.h>

#include "Sensor.h"

#define MS_IN_SEC 1000
#define US_IN_MS 1000
#define MS_IN_MIN 60*MS_IN_SEC
#define ML_IN_LITRE 1000

// Base class for flow sensors that generate pulses from which a flow rate can
// be computed.
class FlowSensor: public virtual Sensor {
  public:
    // Override inherited methods.
    void Setup();
    void Shutdown();
    void Collect();

    String Describe();
    String JSON();
    String MQTTSuffix();

  protected:
    // Plus extras for tracking our pulses and handling them.
    volatile byte pulses;
    virtual float pulseToL(byte p);

    void IRAM_ATTR handleInterrupt();
    std::function<void()> interruptPtr;
    float flowRate;
    unsigned int flowMilliLitres;
    unsigned long totalMilliLitres;
    unsigned long lastTime;
};

// Sensor class for a YFB-10 style flow sensor.
class YFB10FlowSensor: public FlowSensor, SensorTmpl<YFB10FlowSensor> {
  public:
    YFB10FlowSensor(const std::string name_in, const uint8_t pin_in) : Sensor {name_in, pin_in} {}

    static std::string SensorType() { return "YFB10FlowSensor"; };

  protected:
    float pulseToL(byte p);
};

// Sensor class for a FS400A style flow sensor.
class FS400AFlowSensor: public FlowSensor, SensorTmpl<FS400AFlowSensor> {
  public:
    FS400AFlowSensor(const std::string name_in, const uint8_t pin_in) : Sensor {name_in, pin_in} {}

    static std::string SensorType() { return "FS400AFlowSensor"; };
  private:
    float pulseToL(byte p);
};
#endif
// vim: set ts=2 sw=2 sts=2 et:
