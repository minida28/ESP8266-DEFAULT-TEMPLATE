#include <ESP8266NetBIOS.h>

// #include "sholat.h"
// #include "sholathelper.h"
// #include "timehelper.h"
// #include "locationhelper.h"

#include <pgmspace.h>
#include "asyncserver.h"
// #include "buzzer.h"
// #include "mqtt.h"
// #include "asyncpinghelper.h"

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif

// #include "PingAlive.h"

#define DEBUGPORT Serial

// #define RELEASE

#ifndef RELEASE
#define DEBUGLOG(fmt, ...)                   \
  {                                          \
    static const char pfmt[] PROGMEM = fmt;  \
    DEBUGPORT.printf_P(pfmt, ##__VA_ARGS__); \
  }
#else
#define DEBUGLOG(...)
#endif

strConfig _config;
strApConfig _configAP; // Static AP config settings
strHTTPAuth _httpAuth;

Ticker _secondTk;
Ticker restartESP;

//FS* _fs;
unsigned long wifiDisconnectedSince = 0;
String _browserMD5 = "";
uint32_t _updateSize = 0;

uint32_t timestampReceivedFromWebGUI = 0;

bool autoConnect = true;
bool autoReconnect = true;

bool eventsourceTriggered = false;
bool wsConnected = false;
bool configFileNetworkUpdatedFlag = false;
bool configFileLocationUpdated = false;
bool configFileTimeUpdated = false;

WiFiEventHandler onStationModeConnectedHandler, onStationModeGotIPHandler, onStationModeDisconnectedHandler;
WiFiEventHandler stationConnectedHandler;
WiFiEventHandler stationDisconnectedHandler;
WiFiEventHandler probeRequestPrintHandler;
WiFiEventHandler probeRequestBlinkHandler;

bool wifiGotIpFlag = false;
bool wifiDisconnectedFlag = false;
bool stationConnectedFlag = false;
bool stationDisconnectedFlag = false;

bool firmwareUpdated = false;

//SSDP properties
// const char *ssdp_modelName = "ESP8266EX";
// const char *ssdp_modelNumber = "929000226503";

// static const char *ssdpTemplate =
//     "<?xml version=\"1.0\"?>"
//     "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
//     "<specVersion>"
//     "<major>1</major>"
//     "<minor>0</minor>"
//     "</specVersion>"
//     "<URLBase>http://%u.%u.%u.%u/</URLBase>"
//     "<device>"
//     "<deviceType>upnp:rootdevice</deviceType>"
//     "<friendlyName>%s</friendlyName>"
//     "<presentationURL>index.html</presentationURL>"
//     "<serialNumber>%u</serialNumber>"
//     "<modelName>%s</modelName>"
//     "<modelNumber>%s</modelNumber>"
//     "<modelURL>http://www.espressif.com</modelURL>"
//     "<manufacturer>Espressif Systems</manufacturer>"
//     "<manufacturerURL>http://www.espressif.com</manufacturerURL>"
//     "<UDN>uuid:38323636-4558-4dda-9188-cda0e6%02x%02x%02x</UDN>"
//     "</device>"
//     "</root>\r\n"
//     "\r\n";

// static const char *ssdpTemplate =
//     "<?xml version=\"1.0\"?>"
//     "<?xml-stylesheet type=\"text/xsl\" href=\"style.xml\"?>"
//     "<root>"
//     "<specVersion>"
//     "<major>1</major>"
//     "<minor>0</minor>"
//     "</specVersion>"
//     "<URLBase>http://%s</URLBase>"
//     "<device>"
//     "<deviceType>upnp:rootdevice</deviceType>"
//     "<friendlyName>%s</friendlyName>"
//     "<presentationURL>%s</presentationURL>"
//     "<serialNumber>%d</serialNumber>"
//     "<modelName>%s</modelName>"
//     "<modelNumber>%s</modelNumber>"
//     "<modelURL>http://www.espressif.com</modelURL>"
//     "<manufacturer>Espressif Systems</manufacturer>"
//     "<manufacturerURL>http://www.espressif.com</manufacturerURL>"
//     "<UDN>uuid:38323636-4558-4dda-9188-cda0e6%02x%02x%02x</UDN>"
//     "</device>"
//     "</root>\r\n"
//     "\r\n";

// SKETCH BEGIN

FSInfo fs_info;

DNSServer dnsServer;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
uint32_t clientID;

bool sendFreeHeapStatusFlag = false;
bool sendDateTimeFlag = false;
bool setDateTimeFromGUIFlag = false;

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_CONNECT)
  {
    //client connected
    clientID = client->id();
    DEBUGLOG("ws[%s][%u] connect\r\n", server->url(), client->id());
    client->printf("Hello Client %u :)", client->id());
    client->ping();
  }
  else if (type == WS_EVT_DISCONNECT)
  {
    //client disconnected
    DEBUGLOG("ws[%s][%u] disconnect: [%u]\r\n", server->url(), client->id(), client->id());
  }
  else if (type == WS_EVT_ERROR)
  {
    //error was received from the other end
    DEBUGLOG("ws[%s][%u] error(%u): %s\r\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);
  }
  else if (type == WS_EVT_PONG)
  {
    //pong message was received (in response to a ping request maybe)
    DEBUGLOG("ws[%s][%u] pong[%u]: %s\r\n", server->url(), client->id(), len, (len) ? (char *)data : "");
  }
  else if (type == WS_EVT_DATA)
  {
    //data packet
    AwsFrameInfo *info = (AwsFrameInfo *)arg;

    // char msg[len + 1];

    if (info->final && info->index == 0 && info->len == len)
    {
      //the whole message is in a single frame and we got all of it's data
      DEBUGLOG("ws[%s][%u] %s-message[%u]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", (uint32_t)info->len);
      if (info->opcode == WS_TEXT)
      {
        data[len] = 0;
        // os_printf("%s\n", (char *)data);
        DEBUGLOG("%s\n", (char *)data);
      }
      else
      {
        for (size_t i = 0; i < info->len; i++)
        {
          // os_printf("%02x ", data[i]);
          DEBUGLOG("%02x ", data[i]);
        }
        // os_printf("\n");
        DEBUGLOG("\n");
      }
      if (info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    }
    else
    {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0)
      {
        if (info->num == 0)
          DEBUGLOG("ws[%s][%u] %s-message start\r\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
        DEBUGLOG("ws[%s][%u] frame[%u] start[%llu]\r\n", server->url(), client->id(), info->num, info->len);
      }

      DEBUGLOG("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);
      if (info->message_opcode == WS_TEXT)
      {
        data[len] = 0;
        DEBUGLOG("%s\n", (char *)data);
      }
      else
      {
        for (size_t i = 0; i < len; i++)
        {
          DEBUGLOG("%02x ", data[i]);
        }
        DEBUGLOG("\n");
      }

      if ((info->index + len) == info->len)
      {
        DEBUGLOG("ws[%s][%u] frame[%u] end[%llu]\r\n", server->url(), client->id(), info->num, info->len);
        if (info->final)
        {
          DEBUGLOG("ws[%s][%u] %s-message end\r\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
          if (info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }

    if (strncmp_P((char *)data, pgm_schedulepagesholat, strlen_P(pgm_schedulepagesholat)) == 0)
    {
      // clientVisitSholatTimePage = true;
    }
    else if (strncmp_P((char *)data, pgm_settimepage, strlen_P(pgm_settimepage)) == 0)
    {
      sendDateTimeFlag = true;
    }
    else if (strncmp_P((char *)data, pgm_freeheap, strlen_P(pgm_freeheap)) == 0)
    {
      sendFreeHeapStatusFlag = true;
    }
    else if (strncmp((char *)data, "/status/datetime", strlen("/status/datetime")) == 0)
    {
      sendDateTimeFlag = true;
    }
    else if (data[0] == '{')
    {
      StaticJsonDocument<1024> root;
      DeserializationError error = deserializeJson(root, data);

      if (error)
      {
        return;
      }

      //******************************
      // handle SAVE CONFIG (not fast)
      //******************************

      if (root.containsKey(FPSTR(pgm_saveconfig)))
      {
        const char *saveconfig = root[FPSTR(pgm_saveconfig)];

        //remove json key before saving
        root.remove(FPSTR(pgm_saveconfig));

        if (false)
        {
        }
        //******************************
        // save TIME config
        //******************************
        else if (strcmp_P(saveconfig, pgm_configpagetime) == 0)
        {
          File file = SPIFFS.open(FPSTR(pgm_configfiletime), "w");

          if (!file)
          {
            DEBUGLOG("Failed to open TIME config file\r\n");
            file.close();
            return;
          }

          serializeJsonPretty(root, file);
          file.flush();
          file.close();

          configFileTimeUpdated = true;

          //beep
        }
      }
    }
    else if (data[0] == 't' && data[1] == ' ')
    {
      char *token = strtok((char *)&data[2], " ");

      timestampReceivedFromWebGUI = (unsigned long)strtol(token, '\0', 10);

      setDateTimeFromGUIFlag = true;
    }
  }
}

// -------------------------------------------------
// *** FUNCTIONS
// -------------------------------------------------
char *macToString(const unsigned char *mac)
{
  static char buf[20];
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return buf;
}

char *formatBytes(size_t bytes)
{
  static char buf[10];
  if (bytes < 1024)
  {
    itoa(bytes, buf, 10);
    strcat(buf, "B");
  }
  else if (bytes < (1024 * 1024))
  {
    itoa(bytes / 1024.0, buf, 10);
    strcat(buf, "KB");
  }
  else if (bytes < (1024 * 1024 * 1024))
  {
    itoa(bytes / 1024.0 / 1024.0, buf, 10);
    strcat(buf, "MB");
  }
  else
  {
    itoa(bytes / 1024.0 / 1024.0 / 1024.0, buf, 10);
    strcat(buf, "GB");
  }
  return buf;
}

void onWiFiConnected(WiFiEventStationModeConnected data)
{
  DEBUGLOG("\r\nWifi Connected\r\n");
  wifiDisconnectedSince = 0;
  DEBUGLOG("WiFi status: %s\r\n", WiFi.status() == WL_CONNECTED ? "WL_CONNECTED" : String(WiFi.status()).c_str());
}

// Start NTP only after IP network is connected
void onWifiGotIP(WiFiEventStationModeGotIP ipInfo)
// void onWifiGotIP(const WiFiEventStationModeGotIP &event)
{
  wifiGotIpFlag = true;
  WiFi.setAutoReconnect(autoReconnect);

  //Serial.printf_P(PSTR("\r\nWifi Got IP: %s\r\n"), ipInfo.ip.toString().c_str ());
  DEBUGLOG("\r\nWifi Got IP\r\n");
  // DEBUGLOG("IP Address:\t%s\r\n", WiFi.localIP().toString().c_str());
  DEBUGLOG("IP Address:\t%s\r\n", ipInfo.ip.toString().c_str());
}

void onWiFiDisconnected(WiFiEventStationModeDisconnected data)
{
  wifiDisconnectedFlag = true;
  DEBUGLOG("\r\nWifi disconnected.\r\n");
}

void onStationConnected(const WiFiEventSoftAPModeStationConnected &evt)
{
  stationConnectedFlag = true;

  DEBUGLOG("Station connected: %s\r\n", macToString(evt.mac));
}

void onStationDisconnected(const WiFiEventSoftAPModeStationDisconnected &evt)
{
  stationDisconnectedFlag = true;

  DEBUGLOG("Station disconnected: %s\r\n", macToString(evt.mac));
}

void onProbeRequestPrint(const WiFiEventSoftAPModeProbeRequestReceived &evt)
{
  // DEBUGLOG("Probe request from: %s, RSSI: %d\r\n", macToString(evt.mac), evt.rssi);
}

void onProbeRequestBlink(const WiFiEventSoftAPModeProbeRequestReceived &)
{
  // We can't use "delay" or other blocking functions in the event handler.
  // Therefore we set a flag here and then check it inside "loop" function.
  // blinkFlag = true;
}

void handleFileList(AsyncWebServerRequest *request)
{
  if (!request->hasArg("dir"))
  {
    request->send(500, "text/plain", "BAD ARGS");
    return;
  }

  String path = request->arg("dir");
  DEBUGLOG("handleFileList: %s\r\n", path.c_str());
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while (dir.next())
  {
    File entry = dir.openFile("r");
    if (true) //entry.name()!="secret.json") // Do not show secrets
    {
      if (output != "[")
        output += ',';
      bool isDir = false;
      output += "{\"type\":\"";
      output += (isDir) ? "dir" : "file";
      output += "\",\"name\":\"";
      output += String(entry.name()).substring(1);
      output += "\"}";
    }
    entry.close();
  }

  output += "]";
  DEBUGLOG("%s\r\n", output.c_str());
  request->send(200, "application/json", output);
}

bool load_config_network()
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  File file = SPIFFS.open(FPSTR(pgm_configfilenetwork), "r");
  if (!file)
  {
    DEBUGLOG("Failed to open config file\r\n");
    file.close();
    return false;
  }

  // size_t size = file.size();
  DEBUGLOG("JSON file size: %d bytes\r\n", file.size());

  StaticJsonDocument<512> root;
  DeserializationError error = deserializeJson(root, file);

  file.close();

  if (error)
  {
    DEBUGLOG("Failed to parse config NETWORK file\r\n");
    return false;
  }

#ifndef RELEASE
  serializeJsonPretty(root, DEBUGPORT);
  DEBUGLOG("\r\n");
#endif

  // strlcpy(_config.hostname, root[FPSTR(pgm_hostname)], sizeof(_config.hostname));
  strlcpy(_config.ssid, root[FPSTR(pgm_ssid)], sizeof(_config.ssid));
  strlcpy(_config.password, root[FPSTR(pgm_password)], sizeof(_config.password));
  _config.dhcp = root[FPSTR(pgm_dhcp)];
  strlcpy(_config.static_ip, root[FPSTR(pgm_static_ip)], sizeof(_config.static_ip));
  strlcpy(_config.netmask, root[FPSTR(pgm_netmask)], sizeof(_config.netmask));
  strlcpy(_config.gateway, root[FPSTR(pgm_gateway)], sizeof(_config.gateway));
  strlcpy(_config.dns0, root[FPSTR(pgm_dns0)], sizeof(_config.dns0));
  strlcpy(_config.dns1, root[FPSTR(pgm_dns1)], sizeof(_config.dns1));

  DEBUGLOG("\r\nNetwork settings loaded successfully.\r\n");
  DEBUGLOG("hostname: %s\r\n", _config.hostname);
  DEBUGLOG("ssid: %s\r\n", _config.ssid);
  DEBUGLOG("pass: %s\r\n", _config.password);
  DEBUGLOG("dhcp: %d\r\n", _config.dhcp);
  DEBUGLOG("static_ip: %s\r\n", _config.static_ip);
  DEBUGLOG("netmask: %s\r\n", _config.netmask);
  DEBUGLOG("gateway: %s\r\n", _config.gateway);
  DEBUGLOG("dns0: %s\r\n", _config.dns0);
  DEBUGLOG("dns1: %s\r\n", _config.dns1);

  return true;
}

//*************************
// SAVE NETWORK CONFIG
//*************************
bool save_config_network()
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  StaticJsonDocument<1024> json;
  // json[FPSTR(pgm_hostname)] = _config.hostname;
  json[FPSTR(pgm_ssid)] = _config.ssid;
  json[FPSTR(pgm_password)] = _config.password;
  json[FPSTR(pgm_dhcp)] = _config.dhcp;
  json[FPSTR(pgm_static_ip)] = _config.static_ip;
  json[FPSTR(pgm_netmask)] = _config.netmask;
  json[FPSTR(pgm_gateway)] = _config.gateway;
  json[FPSTR(pgm_dns0)] = _config.dns0;
  json[FPSTR(pgm_dns1)] = _config.dns1;

  //TODO add AP data to html
  File file = SPIFFS.open(FPSTR(pgm_configfilenetwork), "w");

#ifndef RELEASE
  serializeJsonPretty(json, DEBUGPORT);
  DEBUGLOG("\r\n");
#endif

  // EEPROM_write_char(eeprom_wifi_ssid_start, eeprom_wifi_ssid_size, _config.ssid);
  // EEPROM_write_char(eeprom_wifi_password_start, eeprom_wifi_password_size, wifi_password);

  serializeJsonPretty(json, file);
  file.flush();
  file.close();
  return true;
}

int pgm_lastIndexOf(uint8_t c, const char *p)
{
  int last_index = -1; // -1 indicates no match
  uint8_t b;
  for (int i = 0; true; i++)
  {
    b = pgm_read_byte(p++);
    if (b == c)
      last_index = i;
    else if (b == 0)
      break;
  }
  return last_index;
}

bool save_system_info()
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  // const char* pathtofile = PSTR(pgm_filesystemoverview);

  // String fileName = FPSTR(pgm_systeminfofile);

  size_t fileLen = strlen_P(pgm_systeminfofile);
  char fileName[fileLen + 1];
  strcpy_P(fileName, pgm_systeminfofile);

  File file;
  if (!SPIFFS.exists(fileName))
  {
    file = SPIFFS.open(fileName, "w");
    if (!file)
    {
      DEBUGLOG("Failed to open %s file for writing\r\n", fileName);
      file.close();
      return false;
    }
    //create blank json file
    DEBUGLOG("Creating %s file for writing\r\n", fileName);
    file.print("{}");
    file.close();
  }
  //get existing json file
  file = SPIFFS.open(fileName, "w");
  if (!file)
  {
    DEBUGLOG("Failed to open %s file", fileName);
    return false;
  }

  const char *the_path = PSTR(__FILE__);
  // const char *_compiletime = PSTR(__TIME__);

  int slash_loc = pgm_lastIndexOf('/', the_path); // index of last '/'
  if (slash_loc < 0)
    slash_loc = pgm_lastIndexOf('\\', the_path); // or last '\' (windows, ugh)

  int dot_loc = pgm_lastIndexOf('.', the_path); // index of last '.'
  if (dot_loc < 0)
    dot_loc = pgm_lastIndexOf(0, the_path); // if no dot, return end of string

  int lenPath = strlen(the_path);
  int lenStrFileName;

  bool useFullPath = true;
  int start_loc = 0;

  if (useFullPath)
  {
    lenStrFileName = lenPath;
    start_loc = 0;
  }
  else
  {
    lenStrFileName = (lenPath - (slash_loc + 1));
    start_loc = slash_loc + 1;
  }

  char strFileName[lenStrFileName + 1];

  //Serial.println(lenFileName);
  //Serial.println(sizeof(fileName));

  int j = 0;
  for (int i = start_loc; i < lenPath; i++)
  {
    uint8_t b = pgm_read_byte(&the_path[i]);
    if (b != 0)
    {
      strFileName[j] = (char)b;
      //Serial.print(strFileName[j]);
      j++;
      if (j >= lenStrFileName)
      {
        break;
      }
    }
    else
    {
      break;
    }
  }
  //Serial.println();
  //Serial.println(j);
  strFileName[lenStrFileName] = '\0';

  //const char* _compiledate = PSTR(__DATE__);
  int lenCompileDate = strlen_P(PSTR(__DATE__));
  char compileDate[lenCompileDate + 1];
  strcpy_P(compileDate, PSTR(__DATE__));

  int lenCompileTime = strlen_P(PSTR(__TIME__));
  char compileTime[lenCompileTime + 1];
  strcpy_P(compileTime, PSTR(__TIME__));

  StaticJsonDocument<1024> root;

  SPIFFS.info(fs_info);

  root[FPSTR(pgm_totalbytes)] = fs_info.totalBytes;
  root[FPSTR(pgm_usedbytes)] = fs_info.usedBytes;
  root[FPSTR(pgm_blocksize)] = fs_info.blockSize;
  root[FPSTR(pgm_pagesize)] = fs_info.pageSize;
  root[FPSTR(pgm_maxopenfiles)] = fs_info.maxOpenFiles;
  root[FPSTR(pgm_maxpathlength)] = fs_info.maxPathLength;

  root[FPSTR(pgm_filename)] = strFileName;
  root[FPSTR(pgm_compiledate)] = compileDate;
  root[FPSTR(pgm_compiletime)] = compileTime;
  // root[FPSTR(pgm_lastboot)] = getLastBootStr();
  root[FPSTR(pgm_chipid)] = ESP.getChipId();
  root[FPSTR(pgm_cpufreq)] = ESP.getCpuFreqMHz();
  root[FPSTR(pgm_sketchsize)] = ESP.getSketchSize();
  root[FPSTR(pgm_freesketchspace)] = ESP.getFreeSketchSpace();
  root[FPSTR(pgm_flashchipid)] = ESP.getFlashChipId();
  root[FPSTR(pgm_flashchipmode)] = ESP.getFlashChipMode();
  root[FPSTR(pgm_flashchipsize)] = ESP.getFlashChipSize();
  root[FPSTR(pgm_flashchiprealsize)] = ESP.getFlashChipRealSize();
  root[FPSTR(pgm_flashchipspeed)] = ESP.getFlashChipSpeed();
  root[FPSTR(pgm_cyclecount)] = ESP.getCycleCount();
  root[FPSTR(pgm_corever)] = ESP.getFullVersion();
  root[FPSTR(pgm_sdkver)] = ESP.getSdkVersion();
  root[FPSTR(pgm_bootmode)] = ESP.getBootMode();
  root[FPSTR(pgm_bootversion)] = ESP.getBootVersion();
  root[FPSTR(pgm_resetreason)] = ESP.getResetReason();

  serializeJsonPretty(root, file);
  file.flush();
  file.close();
  return true;
}

void send_config_network(AsyncWebServerRequest *request)
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  AsyncResponseStream *response = request->beginResponseStream("application/json");
  DynamicJsonDocument root(2048);

  root[FPSTR(pgm_hostname)] = _config.hostname;
  root[FPSTR(pgm_ssid)] = _config.ssid;
  root[FPSTR(pgm_password)] = _config.password;
  root[FPSTR(pgm_dhcp)] = _config.dhcp;
  root[FPSTR(pgm_static_ip)] = _config.static_ip;
  root[FPSTR(pgm_netmask)] = _config.netmask;
  root[FPSTR(pgm_gateway)] = _config.gateway;
  root[FPSTR(pgm_dns0)] = _config.dns0;
  root[FPSTR(pgm_dns1)] = _config.dns1;

  serializeJsonPretty(root, *response);

  request->send(response);
}

void sendHeap(uint8_t mode)
{
  DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  uint32_t heap = ESP.getFreeHeap();

  StaticJsonDocument<256> root;
  root["heap"] = heap;

  size_t len = measureJson(root);
  char buf[len + 1];
  serializeJson(root, buf, len + 1);

  if (mode == 0)
  {
    //
  }
  else if (mode == 1)
  {
    events.send(buf, "heap");
  }
  else if (mode == 2)
  {
    if (ws.hasClient(clientID))
    {
      ws.text(clientID, buf);
    }
    else
    {
      DEBUGLOG("ClientID %d is no longer available.\r\n", clientID);
    }
  }
}
// -------------------------------------------------
// *** END OF FUNCTIONS
// -------------------------------------------------

//--- SETUP
void AsyncWSBegin()
{

  DEBUGLOG("Async WebServer Init...\r\n");

  // Register wifi Event to control connection LED
  onStationModeConnectedHandler = WiFi.onStationModeConnected([](WiFiEventStationModeConnected data) {
    onWiFiConnected(data);
  });
  onStationModeGotIPHandler = WiFi.onStationModeGotIP([](WiFiEventStationModeGotIP data) {
    onWifiGotIP(data);
  });
  // onStationModeGotIPHandler = WiFi.onStationModeGotIP(onWifiGotIP);
  onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected([](WiFiEventStationModeDisconnected data) {
    onWiFiDisconnected(data);
  });

  // Register event handlers.
  // Callback functions will be called as long as these handler objects exist.
  // Call "onStationConnected" each time a station connects
  stationConnectedHandler = WiFi.onSoftAPModeStationConnected(&onStationConnected);
  // Call "onStationDisconnected" each time a station disconnects
  stationDisconnectedHandler = WiFi.onSoftAPModeStationDisconnected(&onStationDisconnected);
  // Call "onProbeRequestPrint" and "onProbeRequestBlink" each time
  // a probe request is received.
  // Former will print MAC address of the station and RSSI to Serial,
  // latter will blink an LED.
  probeRequestPrintHandler = WiFi.onSoftAPModeProbeRequestReceived(&onProbeRequestPrint);
  probeRequestBlinkHandler = WiFi.onSoftAPModeProbeRequestReceived(&onProbeRequestBlink);

  int reading = analogRead(A0);
  DEBUGLOG("Analog Read: %d\r\n", reading);
  if (reading >= 768)
  {                            // analog read 3 volts or more
    _configAP.APenable = true; // Read AP button. If button is pressed, activate AP
    DEBUGLOG("AP Enable = %d\r\n", _configAP.APenable);
  }

  //  if (!_fs) // If SPIFFS is not started
  //    _fs->begin();

  // SPIFFS.format();

  SPIFFS.begin();

  SPIFFS.info(fs_info);

  DEBUGLOG("totalBytes: %u\r\n", fs_info.totalBytes);
  DEBUGLOG("usedBytes: %u\r\n", fs_info.usedBytes);
  DEBUGLOG("blockSize: %u\r\n", fs_info.blockSize);
  DEBUGLOG("pageSize: %u\r\n", fs_info.pageSize);
  DEBUGLOG("maxOpenFiles: %u\r\n", fs_info.maxOpenFiles);
  DEBUGLOG("maxPathLength: %u\r\n", fs_info.maxPathLength);

  // start update firmware
  // Dir dir = SPIFFS.openDir("/");

  // pinMode(LED_BUILTIN, OUTPUT);
  // digitalWrite(LED_BUILTIN, LOW);

  if (SPIFFS.exists("/firmware.bin"))
  {
    File file = SPIFFS.open("/firmware.bin", "r");
    if (!file)
    {
      DEBUGLOG("Failed to open FIRMWARE file\r\n");
      file.close();
      return;
    }
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace, U_FLASH))
    { //start with max available size
      Update.printError(Serial);
      DEBUGLOG("ERROR\r\n");
      file.close();
      return;
    }
    while (file.available())
    {
      uint8_t ibuffer[128];
      file.read((uint8_t *)ibuffer, 128);
      DEBUGLOG("%s", (char *)ibuffer);
      Update.write(ibuffer, sizeof(ibuffer));
      // Serial.DEBUGLOG("#");
    }
    DEBUGLOG("%d", Update.end(true));
    // digitalWrite(LED_BUILTIN, HIGH);
    file.close();
    SPIFFS.remove("/firmware.bin");
  }
  else
  {
    DEBUGLOG("Path to FIRMWARE file not exist.\r\n");
  }

#ifndef RELEASE
  { // List files
    //Dir dir = _fs->openDir("/");
    Dir dir = SPIFFS.openDir("/");
    while (dir.next())
    {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();

      DEBUGLOG("FS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize));
    }
    DEBUGLOG("\r\n");
  }
#endif // RELEASE

  if (!load_config_network())
  { // Try to load configuration from file system
    //defaultConfig(); // Load defaults if any error

    save_config_network();

    _configAP.APenable = true;
  }

  //Set the host name
  char bufPrefix[] = "ESP";
  char bufChipId[11];
  itoa(ESP.getChipId(), bufChipId, 10);

  //char bufHostName[32];
  strlcpy(_config.hostname, bufPrefix, sizeof(_config.hostname));
  strncat(_config.hostname, bufChipId, sizeof(bufChipId));

  //  loadHTTPAuth();

  // _configAP.APenable = true;

  // WiFi.persistent(true);
  // WiFi.mode(WIFI_OFF);

  // bool autoConnect = false;
  // WiFi.setAutoConnect(autoConnect);

  // bool autoReconnect = true;
  // WiFi.setAutoReconnect(autoReconnect);

  DEBUGLOG("WiFi.getListenInterval(): %d\r\n", WiFi.getListenInterval());
  DEBUGLOG("WiFi.isSleepLevelMax(): %d\r\n", WiFi.isSleepLevelMax());

  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  // WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
  // WiFi.setSleepMode(WIFI_MODEM_SLEEP);

  DEBUGLOG("WiFi.getListenInterval(): %d\r\n", WiFi.getListenInterval());
  DEBUGLOG("WiFi.isSleepLevelMax(): %d\r\n", WiFi.isSleepLevelMax());

  WiFi.hostname(_config.hostname);

  // WiFi.mode(WIFI_OFF); //=========== TESTING, ORIGINAL-NYA WIFI DIMATIKAN DULU

  WiFi.setAutoConnect(autoConnect);
  WiFi.setAutoReconnect(autoReconnect);

  if (_configAP.APenable || strcmp(_config.ssid, "") == 0)
  {
    DEBUGLOG("Starting wifi in WIFI_AP mode.\r\n");
    // WiFi.mode(WIFI_OFF);
    // WiFi.mode(WIFI_AP); // bikin error
    WiFi.softAP(_configAP.ssid, _configAP.password);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  }
  else if (!_configAP.APenable)
  {
    if (!_config.dhcp)
    {
      DEBUGLOG("DHCP disabled, starting wifi in WIFI_STA mode with static IP.\r\n");
      // WiFi.mode(WIFI_OFF);
      // WiFi.mode(WIFI_STA);

      IPAddress static_ip;
      IPAddress gateway;
      IPAddress netmask;
      IPAddress dns0;
      IPAddress dns1;

      static_ip.fromString(_config.static_ip);
      gateway.fromString(_config.gateway);
      netmask.fromString(_config.netmask);
      dns0.fromString(_config.dns0);
      dns1.fromString(_config.dns1);

      WiFi.config(static_ip, gateway, netmask, dns0, dns1);
      WiFi.hostname(_config.hostname);
      WiFi.begin(_config.ssid, _config.password);
      WiFi.waitForConnectResult();
    }
    else
    {
      DEBUGLOG("Starting wifi in WIFI_STA mode.\r\n");

      if (WiFi.getAutoReconnect())
      {
        if (WiFi.waitForConnectResult(10000) == -1) // hit timeout
        {
          DEBUGLOG("Wifi connect timeout. Re-starting connection...\r\n");
          WiFi.mode(WIFI_OFF);
          WiFi.hostname(_config.hostname);
          WiFi.begin(_config.ssid, _config.password);
          WiFi.waitForConnectResult();
        }
      }
    }
  }

  dnsServer.start(53, "*", WiFi.softAPIP());

  DEBUGLOG("Starting mDNS responder...\r\n");
  // if (!MDNS.begin(_config.hostname))
  if (!MDNS.begin(_config.hostname))
  { // Start the mDNS responder for esp8266.local
    DEBUGLOG("Error setting up mDNS responder!\r\n");
  }
  else
  {
    DEBUGLOG("mDNS responder started\r\n");
    // MDNS.addService("http", "tcp", 80);
  }

  ArduinoOTA.setHostname(_config.hostname);
  ArduinoOTA.begin();

  NBNS.begin(_config.hostname);

  if (1)
  {
    DEBUGLOG("Starting SSDP...\r\n");
    SSDP.setSchemaURL("description.xml");
    SSDP.setHTTPPort(80);
    SSDP.setDeviceType("upnp:rootdevice");
    // SSDP.setModelName(ssdp_modelName);
    // SSDP.setModelNumber(ssdp_modelNumber);

    // SSDP.setSchemaURL(FPSTR(pgm_descriptionxml));
    // SSDP.setHTTPPort(80);
    // SSDP.setDeviceType(FPSTR(pgm_upnprootdevice));
    //  SSDP.setModelName(_config.deviceName.c_str());
    //  SSDP.setModelNumber(FPSTR(modelNumber));
    SSDP.begin();

    server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
      DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

      File file = SPIFFS.open(FPSTR(pgm_descriptionxmlfile), "r");
      if (!file)
      {
        DEBUGLOG("Failed to open %s file\r\n", file.name());
        file.close();
        return;
      }

      size_t size = file.size();
      DEBUGLOG("%s file size: %d bytes\r\n", file.name(), size);

      // size_t allocatedSize = 1024;
      // if (size > allocatedSize)
      // {
      //   PRINT("WARNING, %s file size %d bytes is larger than allocatedSize %d bytes. Exiting...\r\n", file.name(), size, allocatedSize);
      //   file.close();
      //   return;
      // }

      // Allocate a buffer to store contents of the file
      char buf[size + 1];

      //copy file to buffer
      file.readBytes(buf, size);

      //add termination character at the end
      buf[size] = '\0';

      //close the file, save your memory, keep healthy :-)
      file.close();

      // DEBUGLOG("%s\r\n", buf);

      size_t lenBuf = size;
      DEBUGLOG("Template size: %d bytes\r\n", lenBuf);

      //convert IP address to char array
      size_t len = strlen(WiFi.localIP().toString().c_str());
      char URLBase[len + 1];
      strlcpy(URLBase, WiFi.localIP().toString().c_str(), sizeof(URLBase));

      lenBuf = lenBuf + strlen(URLBase);

      // const char *friendlyName = WiFi.hostname().toString().c_str();
      len = strlen(WiFi.hostname().c_str());
      char friendlyName[len + 1];
      strlcpy(friendlyName, WiFi.hostname().c_str(), sizeof(friendlyName));

      lenBuf = lenBuf + strlen(friendlyName);

      char presentationURL[] = "/";

      lenBuf = lenBuf + strlen(presentationURL);

      uint32_t serialNumber = ESP.getChipId();

      lenBuf = lenBuf + strlen(friendlyName);

      char modelName[] = "ESP8266EX";

      lenBuf = lenBuf + strlen(modelName);
      const char *modelNumber = friendlyName;

      lenBuf = lenBuf + strlen(modelNumber);

      lenBuf = lenBuf + 6;
      DEBUGLOG("Allocated size: %d bytes\r\n", lenBuf);

      StreamString output;

      if (output.reserve(lenBuf))
      {
        output.printf(buf,
                      URLBase,
                      friendlyName,
                      presentationURL,
                      serialNumber,
                      modelName,
                      modelNumber, //modelNumber
                      (uint8_t)((serialNumber >> 16) & 0xff),
                      (uint8_t)((serialNumber >> 8) & 0xff),
                      (uint8_t)serialNumber & 0xff);
        request->send(200, "text/xml", output);
      }
      else
      {
        request->send(500);
      }
    });
  }

  // if (0)
  // {
  //   DEBUGLOG("Starting SSDP...\r\n");
  //   SSDP.setSchemaURL("description.xml");
  //   SSDP.setHTTPPort(80);
  //   SSDP.setDeviceType("upnp:rootdevice");
  //   // SSDP.setModelName(ssdp_modelName);
  //   // SSDP.setModelNumber(ssdp_modelNumber);
  //   SSDP.begin();

  //   server.on("/description.xml", HTTP_GET, [](AsyncWebServerRequest *request) {
  //     DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);

  //     size_t lenBuf = strlen(ssdpTemplate);
  //     DEBUGLOG("Template size: %d bytes\r\n", lenBuf);

  //     //convert IP address to char array
  //     size_t len = strlen(WiFi.localIP().toString().c_str());
  //     char URLBase[len + 1];
  //     strlcpy(URLBase, WiFi.localIP().toString().c_str(), sizeof(URLBase));

  //     lenBuf = lenBuf + strlen(URLBase);

  //     // const char *friendlyName = WiFi.hostname().toString().c_str();
  //     len = strlen(WiFi.hostname().c_str());
  //     char friendlyName[len + 1];
  //     strlcpy(friendlyName, WiFi.hostname().c_str(), sizeof(friendlyName));

  //     lenBuf = lenBuf + strlen(friendlyName);

  //     char presentationURL[] = "/";

  //     lenBuf = lenBuf + strlen(presentationURL);

  //     uint32_t serialNumber = ESP.getChipId();

  //     lenBuf = lenBuf + strlen(friendlyName);

  //     char modelName[] = "ESP8266EX";

  //     lenBuf = lenBuf + strlen(modelName);
  //     const char *modelNumber = friendlyName;

  //     lenBuf = lenBuf + strlen(modelNumber);

  //     lenBuf = lenBuf + 6;
  //     DEBUGLOG("Allocated size: %d bytes\r\n", lenBuf);

  //     StreamString output;
  //     if (output.reserve(lenBuf))
  //     {

  //       // // uint32_t ip = WiFi.localIP();
  //       // IPAddress ip = WiFi.localIP();
  //       // uint32_t chipId = ESP.getChipId();
  //       // char modelName[] = "ESP8266EX";

  //       output.printf(ssdpTemplate,
  //                     URLBase,
  //                     friendlyName,
  //                     presentationURL,
  //                     serialNumber,
  //                     modelName,
  //                     modelNumber, //modelNumber
  //                     (uint8_t)((serialNumber >> 16) & 0xff),
  //                     (uint8_t)((serialNumber >> 8) & 0xff),
  //                     (uint8_t)serialNumber & 0xff);
  //       // request->send(200, "text/xml", (String)output);
  //       request->send(200, "text/xml", output);
  //     }
  //     else
  //     {
  //       request->send(500);
  //     }
  //   });
  // }

  // attach AsyncWebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // attach AsyncEventSource
  server.addHandler(&events);

  events.onConnect([](AsyncEventSourceClient *client) {
    if (client->lastId())
    {
      DEBUGLOG("Client reconnected! Last message ID that it gat is: %u\r\n", client->lastId());
    }
    //send event with message "hello!", id current millis
    // and set reconnect delay to 1 second
    client->send("hello!", NULL, millis(), 1000);
  });

  // HTTP Basic authentication
  // events.setAuthentication("user", "pass");

  server.addHandler(new SPIFFSEditor());

  server.on("/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", __PRETTY_FUNCTION__);
    handleFileList(request);
  });

  server.on("/status/network", [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument root(2048);

    root[FPSTR(pgm_chipid)] = ESP.getChipId();
    root[FPSTR(pgm_hostname)] = WiFi.hostname();
    root[FPSTR(pgm_status)] = FPSTR(wifistatus_P[WiFi.status()]);

    root[FPSTR(pgm_mode)] = FPSTR(wifimode_P[WiFi.getMode()]);
    const char *phymodes[] = {"", "B", "G", "N"};
    root[FPSTR(pgm_phymode)] = phymodes[WiFi.getPhyMode()];
    root[FPSTR(pgm_channel)] = WiFi.channel();
    root[FPSTR(pgm_ssid)] = WiFi.SSID();
    root[FPSTR(pgm_password)] = WiFi.psk();
    root[FPSTR(pgm_encryption)] = WiFi.encryptionType(0);
    root[FPSTR(pgm_isconnected)] = WiFi.isConnected();
    root[FPSTR(pgm_autoconnect)] = WiFi.getAutoConnect();
    root[FPSTR(pgm_persistent)] = WiFi.getPersistent();
    root[FPSTR(pgm_bssid)] = WiFi.BSSIDstr();
    root[FPSTR(pgm_rssi)] = WiFi.RSSI();
    root[FPSTR(pgm_sta_ip)] = WiFi.localIP().toString();
    root[FPSTR(pgm_sta_mac)] = WiFi.macAddress();
    root[FPSTR(pgm_ap_ip)] = WiFi.softAPIP().toString();
    root[FPSTR(pgm_ap_mac)] = WiFi.softAPmacAddress();
    root[FPSTR(pgm_gateway)] = WiFi.gatewayIP().toString();
    root[FPSTR(pgm_netmask)] = WiFi.subnetMask().toString();
    root[FPSTR(pgm_dns0)] = WiFi.dnsIP().toString();
    root[FPSTR(pgm_dns1)] = WiFi.dnsIP(1).toString();

    serializeJson(root, *response);
    request->send(response);
  });

  server.on("/config/network", [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());
    send_config_network(request);
  });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    DEBUGLOG("%s\r\n", request->url().c_str());

    AsyncResponseStream *response = request->beginResponseStream("application/json");
    DynamicJsonDocument doc(2048);
    // JsonArray root = doc.createArray();
    JsonArray root = doc.to<JsonArray>();

    int numberOfNetworks = WiFi.scanComplete();
    if (numberOfNetworks == -2)
    {                          //this may also works: WiFi.scanComplete() == WIFI_SCAN_FAILED
      WiFi.scanNetworks(true); //async enabled
    }
    else if (numberOfNetworks)
    {
      for (int i = 0; i < numberOfNetworks; ++i)
      {
        JsonObject wifi = root.createNestedObject();
        wifi["ssid"] = WiFi.SSID(i);
        wifi["rssi"] = WiFi.RSSI(i);
        wifi["bssid"] = WiFi.BSSIDstr(i);
        wifi["channel"] = WiFi.channel(i);
        wifi["secure"] = WiFi.encryptionType(i);
        wifi["hidden"] = WiFi.isHidden(i) ? true : false;
      }
      WiFi.scanDelete();
      if (WiFi.scanComplete() == -2)
      { //this may also works: WiFi.scanComplete() == WIFI_SCAN_FAILED
        WiFi.scanNetworks(true);
      }
    }
    serializeJson(root, *response);
    request->send(response);
    // example: [{"ssid":"OpenWrt","rssi":-10,"bssid":"A2:F3:C1:FF:05:6A","channel":11,"secure":4,"hidden":false},{"ssid":"DIRECT-sQDESKTOP-7HDAOQDmsTR","rssi":-52,"bssid":"22:F3:C1:F8:B1:E9","channel":11,"secure":4,"hidden":false},{"ssid":"galaxi","rssi":-11,"bssid":"A0:F3:C1:FF:05:6A","channel":11,"secure":4,"hidden":false},{"ssid":"HUAWEI-4393","rssi":-82,"bssid":"D4:A1:48:3C:43:93","channel":11,"secure":4,"hidden":false}]
  });

  // server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("status.html");

  server.begin();

  save_system_info();
}

void AsyncWSLoop()
{
  ArduinoOTA.handle();
  ws.cleanupClients();
  dnsServer.processNextRequest();
  MDNS.update();

  static char serialBuf[1441];
  static size_t bufLen = 0;

  if (Serial.available())
  {
    while (Serial.available() && bufLen < 1440)
    {
      serialBuf[bufLen++] = Serial.read();
      if (!Serial.available() && bufLen < 1440)
      {
        delay(1); //wait a bit more
      }
    }
    serialBuf[bufLen] = 0;
    ws.binaryAll(serialBuf, bufLen);
    // ws2.binaryAll(serialBuf, bufLen);
    bufLen = 0;
  }

  // respond
  if (sendFreeHeapStatusFlag)
  {
    sendFreeHeapStatusFlag = false;
    sendHeap(2);
  }
}
