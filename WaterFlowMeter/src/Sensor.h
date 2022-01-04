/* Base sensor class and registration system.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#ifndef Sensor_h
#define Sensor_h

#include <map>
#include <string>

#include <Arduino.h>

// Base class itself.
class Sensor {

  public:
    Sensor() {};
    Sensor(const std::string name_in, const uint8_t pin_in);

    virtual String Describe();
    virtual String JSON();

    virtual void Setup() {}
    virtual void Shutdown() {}
    virtual void Collect() {}
    virtual String MQTTSuffix();

    static std::string SensorType() { return "Sensor"; };
  protected:
    std::string instance;
    std::string name;
    uint8_t pin;
};

// Factory Class
class SensorFactory {
  public:
    typedef Sensor* (*t_pFactory)(const std::string, const uint8_t);

    static SensorFactory *getFactory() {
      static SensorFactory factory;
      return &factory;
    };

    std::string Register(std::string type, t_pFactory Creator) {
      fMap[type] = Creator;
      return type;
    };

    Sensor *Create(std::string type, const std::string name, const uint8_t pin) {
      return fMap[type](name, pin);
    };

    bool Exists(std::string type) {
      return fMap.find(type) != fMap.end();
    }

    std::map<std::string, t_pFactory> fMap;
};

// Subclass mix-in template
template <typename IMPL>
class SensorTmpl: public virtual Sensor {
  public:
    static Sensor* Create(const std::string name, const uint8_t pin) {
      return new IMPL(name, pin);
    }
    static const std::string TYPE;
  protected:
    SensorTmpl() : Sensor {} { instance = TYPE; }
};
template <typename IMPL>
const std::string SensorTmpl<IMPL>::TYPE = SensorFactory::getFactory()->Register(
    IMPL::SensorType(), &SensorTmpl<IMPL>::Create);

#endif
// vim: set ts=2 sw=2 sts=2 et:
