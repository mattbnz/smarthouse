/* Common utility functions.
 *
 * Copyright ©2021 - Matt Brown. All rights reserved.
 */
#ifndef util_
#define util_h

#include <map>
#include <string>
#include <vector>

#include <LittleFS.h>
#include <PubSubClient.h>

// Print debug output to Serial (if built with -DDEBUG)
void _D(String in);
void _D(std::string in);
void _D(const char *in);
void _DMQTT(PubSubClient *debugClient, String debugTopic);
static PubSubClient *_debugClient;
static String _debugTopic;

// Helper to split a string
std::vector<std::string> Split(std::string s, std::string d);

// Returns int from file, or defaultVal if not present.
int readConfigInt(const char* filename, const int defaultVal);

// Returns contents from file, or defaultVal if not present.
String readConfigString(const char* filename, const String defaultVal);

// Writes to file, returns success or fail
bool writeConfig(const char* filename, const String value);

// Map of Arduino pin mappings to their nice names.
const std::map<std::string, uint8_t> PIN_MAP {
   {"D0", D0}, {"D1", D1}, {"D2", D2}, {"D3", D3}, {"D4", D4}, {"D5", D5},
   {"D6", D6}, {"D7", D7}, {"D8", D8}, {"A0", A0} };

#endif
// vim: set ts=2 sw=2 sts=2 et:
