/*
 * Basic water switch code, for Wemos 1D mini with solenoid controlled
 * via a relay that takes a pin going high as a signal to turn on.
 *
 * Copyright (C) 2021 - Matt Brown
 *
 * All rights reserved.
 */
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <LittleFS.h>
#include <Timer.h>

WiFiServer http(80);

const byte SWITCH_PIN = D1;

// ** Operation Control variables **
// These are stored into flash and read at startup

// Wifi config
String wifiSSID;
String wifiPass;

// OTA enabled?
bool enableOTA = false;

// ** End Operational Control variables **

// ** Internal handlers

// ** Functions follow.

void setup() {
#ifdef DEBUG
  Serial.begin(115200);
  delay(10);
  Serial.println("Booting");
#endif

  pinMode(SWITCH_PIN, OUTPUT);

  // Load our config from flash.
  loadConfig();

  // Don't write WiFI settings to flash (save writes)
  WiFi.persistent(false);

  // Get online.
  connectWIFI();

  // Get ready for OTA (no-op if in low power or not enabled)
  initOTA();

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
  enableOTA = (bool)readConfigInt("enableOTA", enableOTA);
  wifiSSID = readConfigString("wifiSSID", "WaterSwitch");
  wifiPass = readConfigString("wifiPass", "wifipass");
  nodeName = readConfigString("nodeName", "WaterSwitch");
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

void loop() {
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

  client.setTimeout(5000); // default is 1000

  // Read the first line of the request
  String req = client.readStringUntil('\r');
#ifdef DEBUG
  Serial.println(F("request: "));
  Serial.println(req);
#endif

  // Match the request
  int val;
  if (req.indexOf(F("/water/off")) != -1) {
    val = 0;
  } else if (req.indexOf(F("/water/on")) != -1) {
    val = 1;
  } else {
#ifdef DEBUG
    Serial.println(F("invalid request"));
#endif
    val = digitalRead(SWITCH_PIN);
  }

  // Set Relay according to the request
  digitalWrite(SWITCH_PIN, val);

  // read/ignore the rest of the request
  // do not client.flush(): it is for output only, see below
  while (client.available()) {
    // byte by byte is not very efficient
    client.read();
  }

  // Send the response to the client
  // it is OK for multiple small client.print/write,
  // because nagle algorithm will group them into one single packet
  client.print(F("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>\r\nWater is now "));
  client.print((val) ? F("on") : F("off"));
  client.print(F("<br><br>Click <a href='http://"));
  client.print(WiFi.localIP());
  client.print(F("/water/on'>here</a> to switch water on, or <a href='http://"));
  client.print(WiFi.localIP());
  client.print(F("/water/off'>here</a> to switch water off.</html>"));

  // The client will actually be *flushed* then disconnected
  // when the function returns and 'client' object is destroyed (out-of-scope)
  // flush = ensure written data are received by the other side
#ifdef DEBUG
  Serial.println(F("Disconnecting from client"));
#endif
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

// Connect to WiFi if needed.
void connectWIFI() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID, wifiPass);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
#ifdef DEBUG
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
#endif
    ESP.restart();
    return;
  }
  http.begin();
}

// vim: set ts=2 sw=2 sts=2 et:
