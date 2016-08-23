// DS18B20 test.
// 
// Copyright (C) 2012 - Matt Brown
//
// All rights reserved.
#include <JeeLib.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define BUS_PIN 6
OneWire oneWire(BUS_PIN);
DallasTemperature sensors(&oneWire);
int numDevices;
DeviceAddress addresses[1024];

#define NODE_ID 1
#define NODE_GROUP 99
#define SEND_MODE 2   // set to 3 if fuses are e=06/h=DE/l=CE, else set to 2

struct {
  int test;
} payload;

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

  // Initialize the radio
  rf12_initialize(NODE_ID, RF12_868MHZ, NODE_GROUP);
  rf12_control(0xC040); // set low-battery level to 2.2V i.s.o. 3.1V
  rf12_sleep(RF12_SLEEP);
  Serial.println("RF12 radio initialized");
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
  
  Serial.println("Faking report send");
  payload.test++;
  rf12_sleep(RF12_WAKEUP);
  while (!rf12_canSend())
    rf12_recvDone();
  rf12_sendStart(0, &payload, sizeof payload);
  rf12_sendWait(SEND_MODE);
  rf12_sleep(RF12_SLEEP);
  Serial.println("Bogus report sent");
}

// Vim modeline
// vim: set ts=2 sw=2 sts=2 et:
