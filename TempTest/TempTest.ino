// DS18B20 test.
// 
// Copyright (C) 2012 - Matt Brown
//
// All rights reserved.
#include <OneWire.h>
#include <DallasTemperature.h>

#define BUS_PIN 6
OneWire oneWire(BUS_PIN);
DallasTemperature sensors(&oneWire);
int numDevices;
DeviceAddress addresses[1024];

// function to print a device address
void printAddress(DeviceAddress deviceAddress) {
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16) Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

void setup() {
  Serial.begin(57600);
  sensors.begin();
  
  // locate devices on the bus
  numDevices = sensors.getDeviceCount();
  Serial.print("Locating devices...");
  Serial.print("Found ");
  Serial.print(numDevices, DEC);
  Serial.println(" devices.");
  for (int i=0; i<numDevices; i++) {
    Serial.print("Device ");
    Serial.print(i+1);
    Serial.print(" has address ");
    DeviceAddress tmp;
    sensors.getAddress(tmp, i);  
    printAddress(tmp);
    Serial.println("");
    sensors.setResolution(tmp, 12);  // We like our resolution.
  }

  // report parasite power requirements
  Serial.print("Parasite power is: "); 
  if (sensors.isParasitePowerMode()) Serial.println("ON");
  else Serial.println("OFF");
}

void loop() {
  Serial.print("Requesting temperatures...");
  sensors.requestTemperatures(); // Send the command to get temperatures
  Serial.println("DONE");
 
  for (int i=0; i<numDevices; i++) {
    Serial.print("Temperature for Device ");
    Serial.print(i+1);
    Serial.print(" is: ");
    Serial.print(sensors.getTempCByIndex(i)); // Why "byIndex"? You can have more than one IC on the same bus. 0 refers to the first IC on the wire
    Serial.println("");
  }
}

// Vim modeline
// vim: set ts=2 sw=2 sts=2 et:
