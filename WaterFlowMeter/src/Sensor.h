/* Base sensor class and registration system.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#ifndef Sensor_h
#define Sensor_h

#include <map>

#include <Arduino.h>

// Base class itself.
class Sensor {

  public:
    Sensor() {};
    Sensor(const uint8_t pin_in);

    virtual String Describe();
    virtual String JSON();

    virtual void Setup() {}
    virtual void Shutdown() {}
    virtual void Collect() {}
    virtual String MQTTSuffix();

    static std::string SensorType() { return "Sensor"; };
  protected:
    std::string instance;
    uint8_t pin;
};

// Factory Class
class SensorFactory {
  public:
    typedef Sensor* (*t_pFactory)(const uint8_t);

    static SensorFactory *getFactory() {
      static SensorFactory factory;
      return &factory;
    };

    std::string Register(std::string name, t_pFactory Creator) {
      fMap[name] = Creator;
      return name;
    };

    Sensor *Create(std::string name, uint8_t pin) {
      return fMap[name](pin);
    };

    bool Exists(std::string name) {
      return fMap.find(name) != fMap.end();
    }

    std::map<std::string, t_pFactory> fMap;
};

// Subclass mix-in template
template <typename IMPL>
class SensorTmpl: public virtual Sensor {
  public:
    static Sensor* Create(const uint8_t pin) { return new IMPL(pin); }
    static const std::string NAME;
  protected:
    SensorTmpl() : Sensor {} { instance = NAME; }
};
template <typename IMPL>
const std::string SensorTmpl<IMPL>::NAME = SensorFactory::getFactory()->Register(
    IMPL::SensorType(), &SensorTmpl<IMPL>::Create);

#endif
// vim: set ts=2 sw=2 sts=2 et:
