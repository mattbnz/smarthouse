/*
 * Basic water flow meter code, for Wemos 1D mini with a YF-B10 style flow
 * meter attached.
 *
 * Copyright (C) 2019 - Matt Brown
 *
 * All rights reserved.
 */
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

WiFiServer http(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

Timer t;

const byte PUMP1_PIN = D1;
const byte PUMP2_PIN = D5;

struct pumpData {
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
pumpData pump1;
pumpData pump2;
// Have we said hello?
bool helloSent = false;
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
void ICACHE_RAM_ATTR handlePump1Interrupt() {
  pump1.counter++;
}
void ICACHE_RAM_ATTR handlePump2Interrupt() {
  pump2.counter++;
}

// ** Functions follow.
void zeroCounters() {
  reportsSent = 0;
  timeToSleep = false;

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
}

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(10);
  Serial.println("Booting");
#endif

  pinMode(PUMP1_PIN, INPUT);
  pinMode(PUMP2_PIN, INPUT);

  // Load our config from flash.
  loadConfig();

  // Don't write WiFI settings to flash (save writes)
  WiFi.persistent(false);

  // If we're not in low power mode, say hi and set-up config listener.
  if (!lowPower) {
    helloAndConfig();
  }

  // Kick off the reporting loop and set-up to receive interrupts.
  zeroCounters();
  attachInterrupt(digitalPinToInterrupt(PUMP1_PIN), handlePump1Interrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(PUMP2_PIN), handlePump2Interrupt, FALLING);
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
  reportInterval = readConfigInt("reportInterval", reportInterval);
  reportCount = readConfigInt("reportCount", reportCount);
  sleepInterval = readConfigInt("sleepInterval", sleepInterval);
  wifiSSID = readConfigString("wifiSSID", "WaterFlowMeter");
  wifiPass = readConfigString("wifiPass", "wifipass");
  mqttHost = readConfigString("mqttHost", "mqtt");
  mqttPort = readConfigInt("mqttPort", mqttPort);
  nodeName = readConfigString("nodeName", "WaterFlowMeter");
  // Build cached MQTT topic names
  mqttConfigTopic = mqttConfigTopicName("#");
  mqttCh1Topic = mqttTopicName(MQTT_CH1_SUFFIX);
  mqttCh2Topic = mqttTopicName(MQTT_CH2_SUFFIX);
}

// Returns int from file, or defaultVal if not present.
int readConfigInt(const char* filename, const int defaultVal) {
  String contents = readConfigString(filename, "0");
  int v = atoi(contents.c_str());
  #ifdef DEBUG
  Serial.print(filename);
  Serial.print(" config read: ");
  #endif
  if (v != 0) {
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
    "Pump1: mL/min = " + String(pump1.flowRate) +
    ", flow mL = " + String(pump1.flowMilliLitres) +
    ", total mL = " + String(pump1.totalMilliLitres) +
    "; Pump2: mL/min = " + String(pump2.flowRate) +
    ", flow mL = " + String(pump2.flowMilliLitres) +
    ", total mL = " + String(pump2.totalMilliLitres) +
    "\r\n";
  return statusPage;
}

// Called by doReport to convert pulses into flow rates and usage.
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

// called by the timer when it's time to make a report.
void doReport() {
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

    connectMQTT();

#ifdef DEBUG
    Serial.print(WiFi.localIP());
    Serial.print("@");
    Serial.print(pump1_now);
    Serial.print("> pump1: ");
    Serial.print(pump1_pulses);
    Serial.print(" pulses; pump2:  ");
    Serial.print(pump2_pulses);
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
      pump1.flowRate, pump1.flowMilliLitres, pump1.totalMilliLitres);
    mqttClient.publish(mqttCh1Topic.c_str(), message, true);
    sprintf(message,
      "{\"mL_per_min\":%f,\"flow_mL\":%d, \"total_mL\":%ld}",
      pump2.flowRate, pump2.flowMilliLitres, pump2.totalMilliLitres);
    mqttClient.publish(mqttCh2Topic.c_str(), message, true);

    // Update counter, decide if we need to sleep.
    reportsSent++;
    if (lowPower && reportsSent >= reportCount) {
      timeToSleep = true;
    }
}

// Publish a hello and subscribe to config topic if we haven't already.
void helloAndConfig() {
  if (helloSent) {
    return;
  }
  connectMQTT();
  sendConfig();
  mqttClient.subscribe(mqttConfigTopic.c_str());
  helloSent = true;
#ifdef DEBUG
  Serial.println("Said hello and subscribed to config");
#endif
}

void sendConfig() {
  char buf[1024];
  sprintf(buf,"{\"node\":\"%s\",\"lowPower\":%u,"
    "\"reportInterval\":%u, \"reportCount\":%u, \"sleepInterval\":%u}",
    nodeName.c_str(), lowPower, reportInterval, reportCount, sleepInterval);
    mqttClient.publish(MQTT_HELLO_TOPIC, buf, true);
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
  if (stopic.compareTo(mqttConfigTopicName("lowPower")) == 0) {
    if (writeConfig("lowPower", value)) {
      lowPower = (bool)ivalue;
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
  } else {
#ifdef DEBUG
    Serial.println("Unknown config option received!");
#endif
  }
  // Publish our config back so we can verify it was set OK.
  sendConfig();
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
  detachInterrupt(digitalPinToInterrupt(PUMP1_PIN));
  detachInterrupt(digitalPinToInterrupt(PUMP2_PIN));
  t.stop(timer_handle);
  timer_handle = -1;
  timeToSleep = false;
  mqttClient.disconnect();
  WiFi.mode(WIFI_OFF);
  ESP.deepSleep(sleepInterval * US_IN_MS);
}

// vim: set ts=2 sw=2 sts=2 et:
