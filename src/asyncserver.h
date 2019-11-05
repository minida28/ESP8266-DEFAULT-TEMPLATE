#ifndef asyncserver_h
#define asyncserver_h

// #include <Arduino.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <Hash.h>
#include <Ticker.h>
#include <DNSServer.h>
#include <ESP8266SSDP.h>
#include <ESP8266NetBIOS.h>
#include <ESPAsyncTCP.h>
#include <ESP8266SSDP.h>
#include <time.h>
// extern "C" {
// #include "sntp.h"
// }
#elif defined ESP32
#include <WiFi.h>
#include <ESPmDNS.h>
#include "SPIFFS.h"
#include <ESP32Ticker.h>
#include "time.h"
#include "AsyncTCP.h"
#endif

#include <ESPAsyncWebServer.h>

// #include <TimeLib.h>
// #include "sholat.h"
// #include "hub08.h"
// #include "mqtt.h"
#include <Wire.h>
// #include <RtcDS3231.h>      //RTC library
#include <StreamString.h>
#include "AsyncJson.h"
#include <ArduinoJson.h>

#include <ArduinoOTA.h>
#include <SPIFFSEditor.h>


#include "progmemmatrix.h"
// #include "displayhelper.h"

#include <pgmspace.h>

typedef struct {
  char hostname[32] = "ESP_XXXX";
  char ssid[33] = "your_wifi_ssid";
  char password[65] = "your_wifi_password";
  bool dhcp = true;
  char static_ip[16] = "192.168.10.15";
  char netmask[16] = "255.255.255.0";
  char gateway[16] = "192.168.10.1";
  char dns0[16] = "192.168.10.1";
  char dns1[16] = "8.8.8.8";
} strConfig;
extern strConfig _config; // General and WiFi configuration

typedef struct
{
  const char* ssid = _config.hostname; // ChipID is appended to this name
  char password[10] = ""; // NULL means no password
  bool APenable = false; // AP disabled by default
} strApConfig;
extern strApConfig _configAP; // Static AP config settings

typedef struct {
  bool auth;
  char wwwUsername[32];
  char wwwPassword[32];
} strHTTPAuth;
extern strHTTPAuth _httpAuth;


extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern AsyncEventSource events;
extern uint32_t clientID;

extern bool sendDateTimeFlag;



void AsyncWSBegin();
void AsyncWSLoop();


#endif




