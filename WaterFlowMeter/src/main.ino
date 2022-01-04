/*
 * Basic water flow meter code for Wemos D1 mini.
 *
 * Copyright (C) 2019 - Matt Brown
 *
 * All rights reserved.
 */
#include <vector>
#include <string>

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <Timer.h>

#include "util.h"
#include "Sensor.h"
#include "FlowSensor.h"

#define MQTT_TOPIC_PREFIX "smarthouse/"
#define MQTT_HELLO_TOPIC MQTT_TOPIC_PREFIX "hello"
#define MQTT_CONFIG_TOPIC "/config/"
#define MQTT_DEBUG_TOPIC "/debug"

#ifdef DEBUG
  #define VERSION GIT_VERSION "-D"
#endif
#ifndef VERSION
  #define VERSION GIT_VERSION
#endif

WiFiServer http(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

Timer t;

// ** Operation Control variables **
// These are stored into flash and read at startup; new values may be
// received via MQTT at run-time and are persisted to flash.

// Wifi config
String wifiSSID;
String wifiPass;

// MQTT config
String mqttHost;
unsigned int mqttPort = 1883;
String nodeName;

// OTA enabled?
bool enableOTA = false;

// enables a low power consumption mode where we sleep for a period
// before resuming reports per the variables below.
bool lowPower = false;
// how often to report via MQTT
unsigned int reportInterval = 1000;
// how many reports to send before sleeping if lowPower=true
unsigned int reportCount = 0;
// how long to sleep for if lowPower=true
unsigned int sleepInterval = 0;

// Sensor config
String sensorSpec;
// ** End Operational Control variables **

// ** Runtime Status variables **

// Have we said hello?
bool helloSent = false;
// OTA status (reported in hello)
String otaStatus;
// How many reports have we sent
unsigned int reportsSent = 0;
// Handle for the report timer,
// doubles as overall status for are we reporting?
int8_t timer_handle = -1;
// Is it time to go to sleep?
bool timeToSleep = false;
// mqtt topic name caches, built on config read
String mqttConfigTopic;

// What sensors are we working with.
std::vector<Sensor*> sensors;


// ** Functions follow.
void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(10);
  Serial.println("Booting");
#endif

  // Load our config from flash.
  loadConfig();

  // Don't write WiFI settings to flash (save writes)
  WiFi.persistent(false);

  // Get ready for OTA (no-op if in low power or not enabled)
  initOTA();

  // Set-up sensors
  loadSensors();

  // If we're not in low power mode, say hi and set-up config listener.
  if (!lowPower) {
    helloAndConfig();
  }

  // Kick off the reporting loop and set-up to receive interrupts.
  reportsSent = 0;
  timeToSleep = false;
  for (auto s = sensors.begin(); s != sensors.end(); ++s) {
    (*s)->Setup();
  }
  timer_handle = t.every(reportInterval, doReport);

  _D("Setup done");
}

// Parses sensorSpec and initializes the actual sensors.
void loadSensors() {
  std::vector<std::string> sensorSpecs = Split(sensorSpec.c_str(), ";");
  for (std::string s: sensorSpecs) {
    std::vector<std::string> parts = Split(s, ",");
    if (parts.size() != 2) {
      _D("Ignoring bad sensor spec: " + s);
      continue;
    }
    auto fact = SensorFactory::getFactory();
    auto pin = PIN_MAP.find(parts[1]);
    if (!fact->Exists(parts[0])) {
        _D("Ignoring sensor spec with unknown sensor " + s + " (" + parts[0] + ")");
        continue;
    } else if (pin == PIN_MAP.end()) {
        _D("Ignoring sensor spec with unknown pin " + s + " (" + parts[1] + ")");
        continue;
    } else {
      sensors.push_back(fact->Create(parts[0], pin->second));
      _D("Created sensor for " + parts[0] + " on pin " + String(pin->second).c_str());
    }
  }
  _D(String("Sensors Configured: ") + String(sensors.size()));
}

// Initializes the FS and reads config if present.
void loadConfig() {
  if (!LittleFS.begin()) {
    _D("Formatting FS...");
    LittleFS.format();
  }
  lowPower = (bool)readConfigInt("lowPower", lowPower);
  enableOTA = (bool)readConfigInt("enableOTA", enableOTA);
  reportInterval = readConfigInt("reportInterval", reportInterval);
  reportCount = readConfigInt("reportCount", reportCount);
  sleepInterval = readConfigInt("sleepInterval", sleepInterval);
  wifiSSID = readConfigString("wifiSSID", "WaterFlowMeter");
  wifiPass = readConfigString("wifiPass", "wifipass");
  mqttHost = readConfigString("mqttHost", "mqtt");
  mqttPort = readConfigInt("mqttPort", mqttPort);
  nodeName = readConfigString("nodeName", "WaterFlowMeter");
  sensorSpec = readConfigString("sensorSpec", "");
  // Build cached MQTT topic names
  mqttConfigTopic = mqttConfigTopicName("#");
}

void loop() {
  t.update();
  mqttClient.loop();

  if (lowPower) {
    if (timeToSleep) {
      goToSleep();
    }
    return;
  }

  if (enableOTA) {
    ArduinoOTA.handle();
  }

  WiFiClient client = http.available();
  // wait for a client (web browser) to connect
  if (!client) {
    return;
  }

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
    VERSION + " " + String(reportsSent) + " reports sent.\n\n" +
    "Configured Sensors (" + sensorSpec + "):\n";
  for (auto s = sensors.begin(); s != sensors.end(); ++s) {
    statusPage += String("* ") + (*s)->Describe() + String("\n");
  }
  statusPage += String("\nAvailable Sensor Types:\n");
  auto fact = SensorFactory::getFactory();
  for (auto s = fact->fMap.begin(); s != fact->fMap.end(); ++s) {
    statusPage += String("* ") + String(s->first.c_str()) + String("\n");
  }
  statusPage += String("\r\n");
  return statusPage;
}

// called by the timer when it's time to make a report.
void doReport() {
    for (auto s = sensors.begin(); s != sensors.end(); ++s) {
      (*s)->Collect();
    }

    connectMQTT();

    // Make sure we always say hello before sending an update if we haven't
    // already (only used in lowPower mode, since otherwise we'll say hello on
    // boot).
    helloAndConfig();

    // Now sent the update.
    for (auto s = sensors.begin(); s != sensors.end(); ++s) {
      String suffix = (*s)->MQTTSuffix();
      if (suffix == "") {
        continue;
      }
      String topic = mqttTopicName(suffix);
      if (!mqttClient.publish(topic.c_str(), (*s)->JSON().c_str() , true)) {
        _D(String("Failed to publish report to ") + topic);
      }
    }

    // Update counter, decide if we need to sleep.
    reportsSent++;
    if (lowPower && reportsSent >= reportCount) {
      timeToSleep = true;
    }
}

// Publish a hello and subscribe to config topic if we haven't already.
void helloAndConfig() {
  bool otaMsg = false;
  if (!otaStatus.startsWith("disabled") && !otaStatus.startsWith("waiting")) {
    otaMsg = true;
  }
  if (helloSent && !otaMsg) {
    return;
  }
  connectMQTT();
  sendConfig();
  mqttClient.subscribe(mqttConfigTopic.c_str());
  helloSent = true;
  if (otaMsg) {
    otaStatus = "waiting";
  }
  _D("Said hello and subscribed to config");
}

void sendConfig() {
  char buf[1024];
  sprintf(buf,"{\"node\":\"%s\",\"version\":\"" VERSION "\",\"ip\":\"%s\","
    "\"lowPower\":%u,\"otaStatus\":\"%s\",\"wifiSSID\":\"%s\","
    "\"sensorSpec\":\"%s\","
    "\"reportInterval\":%u,\"reportCount\":%u, \"sleepInterval\":%u}",
    nodeName.c_str(), WiFi.localIP().toString().c_str(),
    lowPower, otaStatus.c_str(), wifiSSID.c_str(),
    sensorSpec.c_str(),
    reportInterval, reportCount, sleepInterval);
  if (!mqttClient.publish(MQTT_HELLO_TOPIC, buf, true)) {
    _D("Failed to publish config to hello topic!");
  }
}

void initOTA() {
  if (lowPower) {
    otaStatus = "disabled (low power)";
    return;
  }
  if (!enableOTA) {
    otaStatus = "disabled";
    return;
  }
  otaStatus = "waiting";
  connectMQTT();
  ArduinoOTA.onEnd([]() {
    otaStatus = "OTA completed!";
    _D(otaStatus);
    sendConfig();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    if (error == OTA_AUTH_ERROR) {
      otaStatus = "Auth Failed";
    } else if (error == OTA_BEGIN_ERROR) {
      otaStatus = "Begin Failed";
    } else if (error == OTA_CONNECT_ERROR) {
      otaStatus = "Connect Failed";
    } else if (error == OTA_RECEIVE_ERROR) {
      otaStatus = "Receive Failed";
    } else if (error == OTA_END_ERROR) {
      otaStatus = "End Failed";
    } else {
      String n = String(error);
      otaStatus = "OTA Failed: " + n;
      _D(otaStatus);
    }
    sendConfig();
  });
  ArduinoOTA.begin();
}

// Builds a MQTT topic string for this node. Name must start with /.
String mqttTopicName(String name) {
  // rv == MQTT_PREFIX + nodeName + name
  // e.g. "smarthouse/" + "thisNode" + "/someTopic"
  String rv = String(MQTT_TOPIC_PREFIX);
  rv.concat(nodeName);
  rv.concat(name);
  return rv;
}

// Helper for a config topic (prepends /config/ to the config name)
String mqttConfigTopicName(String name) {
  String rv = MQTT_CONFIG_TOPIC;
  rv.concat(name);
  return mqttTopicName(rv);
}

// Process an incoming MQTT message (aka config update)
void handleConfigMsg(char* topic, byte* payload, unsigned int length) {
  char buf[101];
  if (length > 100) {
    _D("MQTT message too big. Ignoring");
    return;
  }
  memcpy(&buf[0], payload, length);
  buf[length]  = '\0';
  String value = String(buf);
  int ivalue = atoi(value.c_str());
  String stopic = String(topic);
  bool reboot = false;
  if (stopic.compareTo(mqttConfigTopicName("lowPower")) == 0) {
    if (writeConfig("lowPower", value)) {
      lowPower = (bool)ivalue;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("enableOTA")) == 0) {
    if (writeConfig("enableOTA", value)) {
      otaStatus = "updated. rebooting...";
      reboot = true;  // To reconfigure OTA handlers.
    }
  } else if (stopic.compareTo(mqttConfigTopicName("reportInterval")) == 0) {
    if (writeConfig("reportInterval", value)) {
      reportInterval = ivalue;
      // reset reporting timer to new value.
      t.stop(timer_handle);
      timer_handle = t.every(reportInterval, doReport);
    }
  } else if (stopic.compareTo(mqttConfigTopicName("reportCount")) == 0) {
    if (writeConfig("reportCount", value)) {
      reportCount = ivalue;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("sleepInterval")) == 0) {
    if (writeConfig("sleepInterval", value)) {
      sleepInterval = ivalue;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("wifiSSID")) == 0) {
    if (writeConfig("wifiSSID", value)) {
      wifiSSID = value;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("wifiPass")) == 0) {
    if (writeConfig("wifiPass", value)) {
      wifiPass = value;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("mqttHost")) == 0) {
    if (writeConfig("mqttHost", value)) {
      mqttHost = value;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("mqttPort")) == 0) {
    if (writeConfig("mqttPort", value)) {
      mqttPort = ivalue;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("nodeName")) == 0) {
    if (writeConfig("nodeName", value)) {
      nodeName = value;
    }
  } else if (stopic.compareTo(mqttConfigTopicName("sensorSpec")) == 0) {
    if (writeConfig("sensorSpec", value)) {
      sensorSpec = value;
      reboot = true;  // Too much state to try and reset without rebooting.
    }
  } else {
    _D("Unknown config option received!");
  }
  // Publish our config back so we can verify it was set OK.
  sendConfig();
  // Reboot if requested.
  if (reboot) {
    delay(1000); // make sure config above has time to send.
    ESP.restart();
  }
}

// Connect to WiFi if needed.
void connectWIFI() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPass);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    if (!lowPower) {
      _D("Connection Failed! Rebooting...");
      delay(1000);
      ESP.restart();
    }
    return;
  }
  if (!lowPower) {
    http.begin();
  }
}

// Connects to the MQTT server; will bring up WiFi if needed.
void connectMQTT() {
  connectWIFI();
  if (mqttClient.connected()) {
    return;
  }
	
  mqttClient.setServer(mqttHost.c_str(), mqttPort);
  mqttClient.setCallback(handleConfigMsg);
  // Attempt to connect
  if (mqttClient.connect(nodeName.c_str())) {
    _D("MQTT connected");
  } else {
    _D("failed, rc=" + mqttClient.state());
  }
  _DMQTT(&mqttClient, mqttTopicName(MQTT_DEBUG_TOPIC));
}

// Turns off everything before we sleep
void goToSleep() {
  _D("sleeping");
  for (auto s = sensors.begin(); s != sensors.end(); ++s) {
    (*s)->Shutdown();
  }
  t.stop(timer_handle);
  timer_handle = -1;
  timeToSleep = false;
  mqttClient.disconnect();
  WiFi.mode(WIFI_OFF);
  ESP.deepSleep(sleepInterval * US_IN_MS);
}

// vim: set ts=2 sw=2 sts=2 et:
