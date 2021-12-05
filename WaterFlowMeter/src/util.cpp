/* Common utility functions.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#include <vector>
#include <string>

#include <LittleFS.h>

#include "util.h"

// Print debug output to Serial (if built with -DDEBUG)
void _D(String in) {
#ifdef DEBUG
  Serial.println(in);
#endif
}
void _D(std::string in) {
#ifdef DEBUG
  _D(String(in.c_str()));
#endif
}
void _D(const char *in) {
#ifdef DEBUG
  _D(String(in));
#endif
}

// Helper to split a string
std::vector<std::string> Split(std::string s, std::string d) {
    std::vector<std::string> rv;
    size_t start = 0, last;
    size_t dlen = d.length();
    std::string found;

    while ((last = s.find (d, start)) != std::string::npos) {
        found = s.substr(start, last-start);
        start = last + dlen;
        rv.push_back(found);
    }
    rv.push_back(s.substr(start));

    return rv;
}

// Returns int from file, or defaultVal if not present.
int readConfigInt(const char* filename, const int defaultVal) {
  String contents = readConfigString(filename, "-1");
  int v = atoi(contents.c_str());
  if (v != -1) {
    _D(String(filename) + String(" config read: " + String(v)));
    return v;
  } else {
    _D(String(filename) + String(" config read: (default)"));
    return defaultVal;
  }
}

// Returns contents from file, or defaultVal if not present.
String readConfigString(const char* filename, const String defaultVal) {
  char configfile[1024];
  sprintf(configfile, "/%s", filename);
  File f = LittleFS.open(configfile, "r");
  if (!f) {
    _D(String("Couldn't read config from ") + String(configfile));
    return defaultVal;
  }
  String contents = f.readString();
  f.close();
  if (contents == "") {
    _D(String(configfile) + String(" contains nothing! returning default"));
    return defaultVal;
  }
  contents.trim();
  _D(String(configfile) + String(" contains: ") + contents);
  return contents;
}

// Writes to file, returns success or fail
bool writeConfig(const char* filename, const String value) {
  char configfile[1024];
  sprintf(configfile, "/%s", filename);
  File f = LittleFS.open(configfile, "w");
  if (!f) {
    _D(String("Couldn't write to config ") + String(configfile));
    return false;
  }
  f.println(value);
  f.close();
  _D(String("Wrote to ") + String(configfile) + " Contents: " + value);
  return true;
}

// vim: set ts=2 sw=2 sts=2 et:
