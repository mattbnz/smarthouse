/*
 * Basic water flow meter code, for Wemos 1D mini with a YF-B10 style flow
 * meter attached.
 * 
 * Copyright (C) 2019 - Matt Brown
 *
 * All rights reserved.
 */
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Timer.h>

#include "secrets.h"

#define MQTT_CHANNEL_1 "smarthouse/water/flow-meter/1"
#define MQTT_CHANNEL_2 "smarthouse/water/flow-meter/2"

#define MS_IN_SEC 1000
#define MS_IN_MIN 60*MS_IN_SEC
#define ML_IN_LITRE 1000

WiFiServer http(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

Timer t;

const byte PUMP1_PIN = D1;
const byte PUMP2_PIN = D5;

typedef struct pumpData {
    volatile byte counter = 0;
    
    float flowRate;
    unsigned int flowMilliLitres;
    unsigned long totalMilliLitres;
    unsigned long lastTime;
};

pumpData pump1;
pumpData pump2;


void ICACHE_RAM_ATTR handlePump1Interrupt() {
  pump1.counter++;
}
void ICACHE_RAM_ATTR handlePump2Interrupt() {
  pump2.counter++;
}

void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("Booting");

  initWIFI();
  initOTA();
  initMQTT();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pump1.counter = 0;
  pump1.flowRate = 0.0;
  pump1.flowMilliLitres = 0;
  pump1.totalMilliLitres = 0;
  pump1.lastTime = millis();

  pump2.counter = 0;
  pump2.flowRate = 0.0;
  pump2.flowMilliLitres = 0;
  pump2.totalMilliLitres = 0;
  pump2.lastTime = millis();

  t.every(1000, updateFlow);

  pinMode(PUMP1_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PUMP1_PIN), handlePump1Interrupt, FALLING);
  pinMode(PUMP2_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PUMP2_PIN), handlePump2Interrupt, FALLING);
}

void loop() {
  ArduinoOTA.handle();
  t.update();
  
  WiFiClient client = http.available();
  // wait for a client (web browser) to connect
  if (!client) {
    return;
  }
  
  Serial.println("\n[Client connected]");
  while (client.connected()) {
    //client.setSync(true);
    // read line by line what the client (web browser) is requesting
    if (client.available()) {
      String line = client.readStringUntil('\r');
      // wait for end of client's request, that is marked with an empty line
      if (line.length() == 1 && line[0] == '\n') {
        client.println(statusPage());
        break;
      }
    }
  }
}

String statusPage() {
  String statusPage = String("HTTP/1.1 200 OK\r\n") +
    "Content-Type: text/plain\r\n" +
    "Connection: close\r\n" +
    "Refresh: 1\r\n" +
    "\r\n" +
    WiFi.localIP().toString() + "@" + String(millis()) + "> " +
    "Pump1: mL/min = " + String(pump1.flowRate) + 
    ", flow mL = " + String(pump1.flowMilliLitres) +
    ", total mL = " + String(pump1.totalMilliLitres) +
    "; Pump2: mL/min = " + String(pump2.flowRate) + 
    ", flow mL = " + String(pump2.flowMilliLitres) +
    ", total mL = " + String(pump2.totalMilliLitres) +
    "\r\n";
  return statusPage;
}

void updateStats(pumpData *data, const byte pulses, unsigned int now) {
    // per datasheet; pulse characteristic (6*Q-8) Q=L/Min±5%
    // aka pulses=6*L_per_min-8;
    // solved for L_per_min = 1/6*pulses + 4/3
    // except, if we didn't see any pulses, we don't add the constant factor
    // because it's not plausible that we're actually consistently consuming
    // 1.3L/min of water, which is what that would imply.
    if (pulses > 0) {
      data->flowRate = (1.0/6.0)*pulses + (4.0/3.0);  // result is L/min, instantaneous
      data->flowRate *= ML_IN_LITRE; // convert to ML
    } else {
      data->flowRate = 0;
    }
    // divide by fraction of minute that actually passed, to get volume.
    unsigned long elapsed_ms = now - data->lastTime;
    data->flowMilliLitres = data->flowRate / (MS_IN_MIN/elapsed_ms);
    // and update the cumulative counter
    data->totalMilliLitres += data->flowMilliLitres;
    // store the time of the current reading for use next time.
    data->lastTime = now;
}

void updateFlow() {
    unsigned int pump1_now;
    unsigned int pump2_now;
    byte pump1_pulses;
    byte pump2_pulses;
    {
    // Disable interrupts while we read and reset the counter
    detachInterrupt(digitalPinToInterrupt(PUMP1_PIN));

    pump1_now = millis();
    pump1_pulses = pump1.counter;
    pump1.counter = 0;
    
    // Re-enable
    attachInterrupt(digitalPinToInterrupt(PUMP1_PIN), handlePump1Interrupt, FALLING);
    }
    {
    // Disable interrupts while we read and reset the counter
    detachInterrupt(digitalPinToInterrupt(PUMP2_PIN));

    pump2_now = millis();
    pump2_pulses = pump2.counter;
    pump2.counter = 0;
    
    // Re-enable
    attachInterrupt(digitalPinToInterrupt(PUMP2_PIN), handlePump2Interrupt, FALLING);
    }

    updateStats(&pump1, pump1_pulses, pump1_now);
    updateStats(&pump2, pump2_pulses, pump2_now);

    Serial.print(WiFi.localIP());
    Serial.print("@");
    Serial.print(pump1_now);
    Serial.print("> pump1: ");
    Serial.print(pump1_pulses);
    Serial.print(" pulses; pump2:  ");
    Serial.print(pump2_pulses);
    Serial.println(" pulses");

    if (!mqttClient.connected()) {
      reconnectMQTT();
    }
    char message[240];
    sprintf(message,
      "{\"mL_per_min\":%f,\"flow_mL\":%d, \"total_mL\":%ld}",
      pump1.flowRate, pump1.flowMilliLitres, pump1.totalMilliLitres);
    mqttClient.publish(MQTT_CHANNEL_1, message, true);
    sprintf(message,
      "{\"mL_per_min\":%f,\"flow_mL\":%d, \"total_mL\":%ld}",
      pump2.flowRate, pump2.flowMilliLitres, pump2.totalMilliLitres);
    mqttClient.publish(MQTT_CHANNEL_2, message, true);
}

void initOTA() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}


void initWIFI() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  http.begin();
}

void initMQTT() {
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
}

void reconnectMQTT() {
  // Loop until we're reconnected
  Serial.println("Attempting MQTT connection...");
  // Attempt to connect
  if (mqttClient.connect(MQTT_CLIENT_ID)) {
    Serial.println("connected");
  } else {
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
  }
}

