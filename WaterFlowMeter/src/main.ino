/*
 * Basic water flow meter code, for Wemos 1D mini with a YF-B10 style flow
 * meter attached.
 *
 * Copyright (C) 2019 - Matt Brown
 *
 * All rights reserved.
 */
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <PubSubClient.h>
#include <Timer.h>

#define MQTT_TOPIC_PREFIX "smarthouse/"
#define MQTT_HELLO_TOPIC MQTT_TOPIC_PREFIX "hello"
#define MQTT_CONFIG_TOPIC "/config/"
#define MQTT_CH1_SUFFIX "/flow-meter/1"
#define MQTT_CH2_SUFFIX "/flow-meter/2"

#define MS_IN_SEC 1000
#define US_IN_MS 1000
#define MS_IN_MIN 60*MS_IN_SEC
#define ML_IN_LITRE 1000

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

#define NUM_PINS 10
const uint8_t PIN_MAP[NUM_PINS] = {D0, D1, D2, D3, D4, D5, D6, D7, D8, A0};

struct flowData {
    uint8_t pin = 0;

    volatile byte counter = 0;

    float flowRate;
    unsigned int flowMilliLitres;
    unsigned long totalMilliLitres;
    unsigned long lastTime;
};

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
// ** End Operational Control variables **

// ** Runtime Status variables **
flowData flow1;
flowData flow2;
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
String mqttCh1Topic;
String mqttCh2Topic;

// ** Internal handlers
void IRAM_ATTR handleFlow1Interrupt() {
  flow1.counter++;
}
void IRAM_ATTR handleFlow2Interrupt() {
  flow2.counter++;
}

// ** Functions follow.
void zeroCounters() {
  reportsSent = 0;
  timeToSleep = false;

  flow1.counter = 0;
  flow1.flowRate = 0.0;
  flow1.flowMilliLitres = 0;
  flow1.totalMilliLitres = 0;
  flow1.lastTime = millis();

  flow2.counter = 0;
  flow2.flowRate = 0.0;
  flow2.flowMilliLitres = 0;
  flow2.totalMilliLitres = 0;
  flow2.lastTime = millis();
}

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

  // If we're not in low power mode, say hi and set-up config listener.
  if (!lowPower) {
    helloAndConfig();
  }

  // Kick off the reporting loop and set-up to receive interrupts.
  zeroCounters();
  pinMode(flow1.pin, INPUT);
  pinMode(flow2.pin, INPUT);
  attachInterrupt(digitalPinToInterrupt(flow1.pin), handleFlow1Interrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(flow2.pin), handleFlow2Interrupt, FALLING);
  timer_handle = t.every(reportInterval, doReport);

#ifdef DEBUG
  Serial.println("Setup done");
#endif
}

// Initializes the FS and reads config if present.
void loadConfig() {
  if (!LittleFS.begin()) {
#ifdef DEBUG
    Serial.println("Formatting FS...");
#endif
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
  flow1.pin = readConfigInt("flow1Pin", D6);
  flow2.pin = readConfigInt("flow2Pin", D5);
  // Build cached MQTT topic names
  mqttConfigTopic = mqttConfigTopicName("#");
  mqttCh1Topic = mqttTopicName(MQTT_CH1_SUFFIX);
  mqttCh2Topic = mqttTopicName(MQTT_CH2_SUFFIX);
}

// Returns int from file, or defaultVal if not present.
int readConfigInt(const char* filename, const int defaultVal) {
  String contents = readConfigString(filename, "-1");
  int v = atoi(contents.c_str());
  #ifdef DEBUG
  Serial.print(filename);
  Serial.print(" config read: ");
  #endif
  if (v != -1) {
    #ifdef DEBUG
    Serial.println(v);
    #endif
    return v;
  } else {
    #ifdef DEBUG
    Serial.println(" (default)");
    #endif
    return defaultVal;
  }
}

// Returns contents from file, or defaultVal if not present.
String readConfigString(const char* filename, const String defaultVal) {
  char configfile[1024];
  sprintf(configfile, "/%s", filename);
  File f = LittleFS.open(configfile, "r");
  if (!f) {
    #ifdef DEBUG
    Serial.print("Couldn't read config from ");
    Serial.println(configfile);
    #endif
    return defaultVal;
  }
  String contents = f.readString();
  f.close();
  #ifdef DEBUG
  Serial.print(configfile);
  Serial.print(" contains: ");
  #endif
  if (contents == "") {
    #ifdef DEBUG
    Serial.println(" nothing! returning default");
    #endif
    return defaultVal;
  }
  contents.trim();
  #ifdef DEBUG
  Serial.println(contents);
  #endif
  return contents;
}

// Writes to file, returns success or fail
bool writeConfig(const char* filename, const String value) {
  char configfile[1024];
  sprintf(configfile, "/%s", filename);
  File f = LittleFS.open(configfile, "w");
  if (!f) {
    #ifdef DEBUG
    Serial.print("Couldn't write to config ");
    Serial.println(configfile);
    #endif
    return false;
  }
  f.println(value);
  f.close();
#ifdef DEBUG
  Serial.print("Wrote to ");
  Serial.println(configfile);
  Serial.print("Contents: ");
  Serial.println(value);
#endif
  return true;
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

#ifdef DEBUG
  Serial.println("\n[Client connected]");
#endif
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
    "Flow1: mL/min = " + String(flow1.flowRate) +
    ", flow mL = " + String(flow1.flowMilliLitres) +
    ", total mL = " + String(flow1.totalMilliLitres) +
    "; Flow2: mL/min = " + String(flow2.flowRate) +
    ", flow mL = " + String(flow2.flowMilliLitres) +
    ", total mL = " + String(flow2.totalMilliLitres) +
    "; version: " + VERSION +
    "\r\n";
  return statusPage;
}

// Called by doReport to convert pulses into flow rates and usage.
void updateStats(flowData *data, const byte pulses, unsigned int now) {
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

// called by the timer when it's time to make a report.
void doReport() {
    unsigned int flow1_now;
    unsigned int flow2_now;
    byte flow1_pulses;
    byte flow2_pulses;
    {
    // Disable interrupts while we read and reset the counter
    detachInterrupt(digitalPinToInterrupt(flow1.pin));

    flow1_now = millis();
    flow1_pulses = flow1.counter;
    flow1.counter = 0;

    // Re-enable
    attachInterrupt(digitalPinToInterrupt(flow1.pin), handleFlow1Interrupt, FALLING);
    }
    {
    // Disable interrupts while we read and reset the counter
    detachInterrupt(digitalPinToInterrupt(flow2.pin));

    flow2_now = millis();
    flow2_pulses = flow2.counter;
    flow2.counter = 0;

    // Re-enable
    attachInterrupt(digitalPinToInterrupt(flow2.pin), handleFlow2Interrupt, FALLING);
    }

    updateStats(&flow1, flow1_pulses, flow1_now);
    updateStats(&flow2, flow2_pulses, flow2_now);

    connectMQTT();

#ifdef DEBUG
    Serial.print(WiFi.localIP());
    Serial.print("@");
    Serial.print(flow1_now);
    Serial.print("> flow1: ");
    Serial.print(flow1_pulses);
    Serial.print(" pulses; flow2:  ");
    Serial.print(flow2_pulses);
    Serial.println(" pulses");
#endif

    // Make sure we always say hello before sending an update if we haven't
    // already (only used in lowPower mode, since otherwise we'll say hello on
    // boot).
    helloAndConfig();

    // Now sent the update.
    char message[240];
    sprintf(message,
      "{\"mL_per_min\":%f,\"flow_mL\":%d, \"total_mL\":%ld}",
      flow1.flowRate, flow1.flowMilliLitres, flow1.totalMilliLitres);
    mqttClient.publish(mqttCh1Topic.c_str(), message, true);
    sprintf(message,
      "{\"mL_per_min\":%f,\"flow_mL\":%d, \"total_mL\":%ld}",
      flow2.flowRate, flow2.flowMilliLitres, flow2.totalMilliLitres);
    mqttClient.publish(mqttCh2Topic.c_str(), message, true);

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
#ifdef DEBUG
  Serial.println("Said hello and subscribed to config");
#endif
}

// Converts a numeric pin into a readable description.
String pinToString(const uint8_t pin) {
  // Try digital pins first.
  for (int i=0; i<NUM_PINS-1; i++) {
    if (PIN_MAP[i] == pin) {
      return String("D") + String(i, DEC);
    }
  }
  // Then the analog pin we keep at the end.
  if (PIN_MAP[NUM_PINS-1] == pin) {
    return String("A0");
  }
  // Not known.
  return String("UNKNOWN");
}

void sendConfig() {
  char buf[1024];
  sprintf(buf,"{\"node\":\"%s\",\"version\":\"" VERSION "\",\"ip\":\"%s\","
    "\"lowPower\":%u,\"otaStatus\":\"%s\",\"wifiSSID\":\"%s\","
    "\"flow1Pin\":\"%s\",\"flow2Pin\":\"%s\","
    "\"reportInterval\":%u,\"reportCount\":%u, \"sleepInterval\":%u}",
    nodeName.c_str(), WiFi.localIP().toString().c_str(),
    lowPower, otaStatus.c_str(), wifiSSID.c_str(),
    pinToString(flow1.pin).c_str(), pinToString(flow2.pin).c_str(),
    reportInterval, reportCount, sleepInterval);
  mqttClient.publish(MQTT_HELLO_TOPIC, buf, true);
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
#ifdef DEBUG
  Serial.println("MQTT callback");
#endif
  char buf[101];
  if (length > 100) {
#ifdef DEBUG
    Serial.println("MQTT message too big. Ignoring");
    return;
#endif
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
  } else if (stopic.compareTo(mqttConfigTopicName("flow1Pin")) == 0) {
    if (handlePinConfig("flow1Pin", &flow1.pin, ivalue)) {
      reboot = true; // To reset interrupt handlers, etc
    }
  } else if (stopic.compareTo(mqttConfigTopicName("flow2Pin")) == 0) {
    if (handlePinConfig("flow2Pin", &flow2.pin, ivalue)) {
      reboot = true; // To reset interrupt handlers, etc
    }
  } else {
#ifdef DEBUG
    Serial.println("Unknown config option received!");
#endif
  }
  // Publish our config back so we can verify it was set OK.
  sendConfig();
  // Reboot if requested.
  if (reboot) {
    delay(1000); // make sure config above has time to send.
    ESP.restart();
  }
}

// Helper to process a pin config update
bool handlePinConfig(const char* varname, uint8_t *livepin, const int ivalue) {
  if (ivalue <0 || ivalue >= NUM_PINS) {
    return false;
  }
  uint8_t pin = PIN_MAP[ivalue];
  char cbuf[4];
  sprintf(cbuf, "%d", int(pin));
  if (writeConfig(varname, cbuf)) {
    // make sure the updated config shows this immediate, but the reboot does
    // the real work of reconfiguring.
    *livepin = pin;
    return true;
  }
  return false;
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
#ifdef DEBUG
      Serial.println("Connection Failed! Rebooting...");
      delay(5000);
#endif
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
#ifdef DEBUG
  Serial.println("Attempting MQTT connection...");
#endif
  // Attempt to connect
  if (mqttClient.connect(nodeName.c_str())) {
#ifdef DEBUG
    Serial.println("connected");
#endif
  } else {
#ifdef DEBUG
    Serial.print("failed, rc=");
    Serial.println(mqttClient.state());
#endif
  }
}

// Turns off everything before we sleep
void goToSleep() {
#ifdef DEBUG
  Serial.println("sleeping");
#endif
  detachInterrupt(digitalPinToInterrupt(flow1.pin));
  detachInterrupt(digitalPinToInterrupt(flow2.pin));
  t.stop(timer_handle);
  timer_handle = -1;
  timeToSleep = false;
  mqttClient.disconnect();
  WiFi.mode(WIFI_OFF);
  ESP.deepSleep(sleepInterval * US_IN_MS);
}

// vim: set ts=2 sw=2 sts=2 et:
