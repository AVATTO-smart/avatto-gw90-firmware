#include <WiFi.h>
// #include <WiFiClient.h>
// #include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <Update.h>
#include <Ticker.h>
#include <esp_wifi.h>
#include <ETH.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <CCTools.h>
#include <WireGuard-ESP32.h>
#include <CronAlarms.h>

#include "config.h"
#include "web.h"
#include "log.h"
#include "etc.h"
#include "mqtt.h"
#include "zb.h"
#include "version.h"

#ifdef ETH_CLK_MODE
#undef ETH_CLK_MODE
#endif

#define BUFFER_SIZE 256

ConfigSettingsStruct ConfigSettings;
zbVerStruct zbVer;
// InfosStruct Infos;
MqttSettingsStruct MqttSettings;
WgSettingsStruct WgSettings;

// volatile bool btnFlag = false;
int btnFlag = false;
bool updWeb = false;

void mDNS_start();
void connectWifi();
void handleLongBtn();
void handleTmrNetworkOverseer();
void setupCoordinatorMode();
void networkStart();
void startAP(const bool start);
IPAddress parse_ip_address(const char *str);

Ticker tmrBtnLongPress(handleLongBtn, 1000, 0, MILLIS);
Ticker tmrNetworkOverseer(handleTmrNetworkOverseer, overseerInterval, 0, MILLIS);

IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
WiFiServer server(TCP_LISTEN_PORT, MAX_SOCKET_CLIENTS);

static WireGuard wg;

extern CCTools CCTool;
// MDNSResponder MDNS; don't need?

void initLan()
{
  if (ETH.begin(ETH_ADDR_1, ETH_POWER_PIN_1, ETH_MDC_PIN_1, ETH_MDIO_PIN_1, ETH_TYPE_1, ETH_CLK_MODE_1, ETH_POWER_PIN_ALTERNATIVE_1))
  {
    DEBUG_PRINTLN(F("LAN start ok"));
    if (!ConfigSettings.dhcp)
    {
      DEBUG_PRINTLN(F("ETH STATIC"));
      ETH.config(parse_ip_address(ConfigSettings.ipAddress), parse_ip_address(ConfigSettings.ipGW), parse_ip_address(ConfigSettings.ipMask));
    }
  }
  else
  {
    DEBUG_PRINTLN(F("LAN start err"));
  }
}

void startSocketServer()
{
  server.begin(ConfigSettings.socketPort);
  server.setNoDelay(true);
}

void wgBegin()
{
  if (!wg.is_initialized())
  {
    //printLogMsg(String("Initializing WireGuard interface..."));
    if (!wg.begin(
            WgSettings.localIP,
            WgSettings.localPrivKey,
            WgSettings.endAddr,
            WgSettings.endPubKey,
            WgSettings.endPort))
    {
      printLogMsg(String("Failed to initialize WG"));
      WgSettings.init = false;
    }
    else
    {
      printLogMsg(String("WG was initialized"));
      WgSettings.init = true;
    }
  }
}

void startServers(bool usb = false)
{
  initWebServer();
  if (!usb)
    startSocketServer();
  // Only close AP if we have a successful network connection (WiFi or LAN)
  // This prevents closing AP when it's the only way to access the device
  if (!usb && (WiFi.isConnected() || ConfigSettings.connectedEther))
  {
    startAP(false); // Close AP when we have network connectivity
  }
  mDNS_start();
  getZbVer();
  if (WgSettings.enable)
  {
    wgBegin();
  }
}

void handleTmrNetworkOverseer()
{
  switch (ConfigSettings.coordinator_mode)
  {
  case COORDINATOR_MODE_WIFI:
    DEBUG_PRINT(F("WiFi.status = "));
    DEBUG_PRINTLN(WiFi.status());
    
    if (WiFi.isConnected())
    {
      DEBUG_PRINTLN(F("WIFI CONNECTED"));
      tmrNetworkOverseer.stop();
      startServers();
    }
    else
    {
      if (tmrNetworkOverseer.counter() > overseerMaxRetry)
      {
        DEBUG_PRINTLN(F("WIFI counter overflow"));
        startAP(true);
        connectWifi();
        tmrNetworkOverseer.stop();
      }
    }
    break;
    
  case COORDINATOR_MODE_LAN:
    if (ConfigSettings.connectedEther)
    {
      DEBUG_PRINTLN(F("LAN CONNECTED"));
      tmrNetworkOverseer.stop();
      startServers();
      if (ConfigSettings.apStarted)
      {
        startAP(false);
      }
    }
    else
    {
      if (tmrNetworkOverseer.counter() > overseerMaxRetry)
      {
        DEBUG_PRINTLN(F("LAN counter overflow"));
        startAP(true);
        tmrNetworkOverseer.stop();
      }
    }
    break;
    
  case COORDINATOR_MODE_USB:
    if (ConfigSettings.keepWeb)
    {
      if (WiFi.isConnected() || ConfigSettings.connectedEther)
      {
        DEBUG_PRINTLN(F("USB mode: Network connected"));
        tmrNetworkOverseer.stop();
        startServers(true);
      }
      else if (tmrNetworkOverseer.counter() > overseerMaxRetry)
      {
        if (!ConfigSettings.apStarted)
        {
          startAP(true);
        }
        tmrNetworkOverseer.stop();
      }
    }
    break;

  default:
    break;
  }
}

void NetworkEvent(WiFiEvent_t event)
{
  DEBUG_PRINT(F("NetworkEvent "));
  DEBUG_PRINTLN(event);
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    DEBUG_PRINTLN(F("ETH Started"));
    ETH.setHostname(ConfigSettings.hostname);
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    DEBUG_PRINTLN(F("ETH Connected"));
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    DEBUG_PRINTLN(F("ETH MAC: "));
    DEBUG_PRINT(ETH.macAddress());
    DEBUG_PRINT(F(", IPv4: "));
    DEBUG_PRINT(ETH.localIP());
    if (ETH.fullDuplex())
    {
      DEBUG_PRINT(F(", FULL_DUPLEX"));
    }
    DEBUG_PRINT(F(", "));
    DEBUG_PRINT(ETH.linkSpeed());
    DEBUG_PRINTLN(F("Mbps"));
    ConfigSettings.connectedEther = true;
    setClock();
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED: // 21:  //SYSTEM_EVENT_ETH_DISCONNECTED:
    DEBUG_PRINTLN(F("ETH Disconnected"));
    ConfigSettings.connectedEther = false;
    if (tmrNetworkOverseer.state() == STOPPED)
    {
      tmrNetworkOverseer.start();
    }
    break;
  case SYSTEM_EVENT_ETH_STOP:
  case ARDUINO_EVENT_ETH_STOP:
    DEBUG_PRINTLN(F("ETH Stopped"));
    ConfigSettings.connectedEther = false;
    if (tmrNetworkOverseer.state() == STOPPED)
    {
      tmrNetworkOverseer.start();
    }
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    DEBUG_PRINTLN(F("WiFi"));
    DEBUG_PRINT(F("IPv4: "));
    DEBUG_PRINT(WiFi.localIP().toString());
    DEBUG_PRINT(F(", "));
    DEBUG_PRINT(WiFi.subnetMask().toString());
    DEBUG_PRINT(F(", "));
    DEBUG_PRINTLN(WiFi.gatewayIP().toString());
    setClock();
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: // SYSTEM_EVENT_STA_DISCONNECTED:
    DEBUG_PRINTLN(F("WIFI STA DISCONNECTED"));
    if (tmrNetworkOverseer.state() == STOPPED)
    {
      tmrNetworkOverseer.start();
    }
    break;
  default:
    break;
  }
}

IPAddress parse_ip_address(const char *str)
{
  IPAddress result;
  int index = 0;

  result[0] = 0;
  while (*str)
  {
    if (isdigit((unsigned char)*str))
    {
      result[index] *= 10;
      result[index] += *str - '0';
    }
    else
    {
      index++;
      if (index < 4)
      {
        result[index] = 0;
      }
    }
    str++;
  }

  return result;
}

bool loadSystemVar()
{ // todo remove
  File configFile = LittleFS.open(configFileSystem, FILE_READ);
  if (!configFile)
  {
    DEBUG_PRINTLN(F("failed open. try to write defaults"));

    float CPUtemp = getCPUtemp(true);
    int correct = CPUtemp - 30;
    String tempOffset = String(correct);

    String StringConfig = "{\"emergencyWifi\":0,\"tempOffset\":" + tempOffset + "}";
    DEBUG_PRINTLN(StringConfig);
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, StringConfig);

    File configFile = LittleFS.open(configFileSystem, FILE_WRITE);
    if (!configFile)
    {
      DEBUG_PRINTLN(F("failed write"));
      return false;
    }
    else
    {
      serializeJson(doc, configFile);
    }
    return false;
  }

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, configFile);

  ConfigSettings.tempOffset = (int)doc["tempOffset"];
  if (!ConfigSettings.tempOffset)
  {
    DEBUG_PRINTLN(F("no tempOffset in system.json"));
    configFile.close();

    float CPUtemp = getCPUtemp(true);
    int correct = CPUtemp - 30;
    String tempOffset = String(correct);
    doc["tempOffset"] = int(tempOffset.toInt());

    configFile = LittleFS.open(configFileSystem, FILE_WRITE);
    serializeJson(doc, configFile);
    configFile.close();
    DEBUG_PRINTLN(F("saved tempOffset in system.json"));
    ConfigSettings.tempOffset = int(tempOffset.toInt());
  }
  configFile.close();
  return true;
}

bool loadConfigWifi()
{
  File configFile = LittleFS.open(configFileWifi, FILE_READ);
  const char *enableWiFi = "enableWiFi";
  const char *ssid = "ssid";
  const char *pass = "pass";
  const char *dhcpWiFi = "dhcpWiFi";
  const char *ip = "ip";
  const char *mask = "mask";
  const char *gw = "gw";
  if (!configFile)
  {
    // String StringConfig = "{\"enableWiFi\":0,\"ssid\":\"\",\"pass\":\"\",\"dhcpWiFi\":1,\"ip\":\"\",\"mask\":\"\",\"gw\":\"\",\"disableEmerg\":1}";
    DynamicJsonDocument doc(1024);
    doc[enableWiFi] = 0;
    doc[ssid] = "";
    doc[pass] = "";
    doc[dhcpWiFi] = 1;
    doc[ip] = "";
    doc[mask] = "";
    doc[gw] = "";
    writeDefaultConfig(configFileWifi, doc);
  }

  configFile = LittleFS.open(configFileWifi, FILE_READ);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);

  if (error)
  {
    DEBUG_PRINTLN(F("deserializeJson() failed: "));
    DEBUG_PRINTLN(error.f_str());

    configFile.close();
    LittleFS.remove(configFileWifi);
    return false;
  }

  ConfigSettings.dhcpWiFi = (int)doc[dhcpWiFi];
  strlcpy(ConfigSettings.ssid, doc[ssid] | "", sizeof(ConfigSettings.ssid));
  strlcpy(ConfigSettings.password, doc[pass] | "", sizeof(ConfigSettings.password));
  strlcpy(ConfigSettings.ipAddressWiFi, doc[ip] | "", sizeof(ConfigSettings.ipAddressWiFi));
  strlcpy(ConfigSettings.ipMaskWiFi, doc[mask] | "", sizeof(ConfigSettings.ipMaskWiFi));
  strlcpy(ConfigSettings.ipGWWiFi, doc[gw] | "", sizeof(ConfigSettings.ipGWWiFi));
  // ConfigSettings.enableWiFi = (int)doc["enableWiFi"];
  // ConfigSettings.disableEmerg = (int)doc["disableEmerg"];

  configFile.close();
  return true;
}

bool loadConfigEther()
{
  const char *dhcp = "dhcp";
  const char *ip = "ip";
  const char *mask = "mask";
  const char *gw = "gw";
  File configFile = LittleFS.open(configFileEther, FILE_READ);
  if (!configFile)
  {
    DynamicJsonDocument doc(1024);
    doc[dhcp] = 1;
    doc[ip] = "";
    doc[mask] = "";
    doc[gw] = "";
    // doc["disablePingCtrl"] = 0;
    // String StringConfig = "{\"dhcp\":1,\"ip\":\"\",\"mask\":\"\",\"gw\":\"\",\"disablePingCtrl\":0}";
    writeDefaultConfig(configFileEther, doc);
  }

  configFile = LittleFS.open(configFileEther, FILE_READ);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);

  if (error)
  {
    DEBUG_PRINTLN(F("deserializeJson() failed: "));
    DEBUG_PRINTLN(error.f_str());

    configFile.close();
    LittleFS.remove(configFileEther);
    return false;
  }

  ConfigSettings.dhcp = (int)doc[dhcp];
  strlcpy(ConfigSettings.ipAddress, doc[ip] | "", sizeof(ConfigSettings.ipAddress));
  strlcpy(ConfigSettings.ipMask, doc[mask] | "", sizeof(ConfigSettings.ipMask));
  strlcpy(ConfigSettings.ipGW, doc[gw] | "", sizeof(ConfigSettings.ipGW));
  // ConfigSettings.disablePingCtrl = (int)doc["disablePingCtrl"];

  configFile.close();
  return true;
}

bool loadConfigGeneral()
{
  const char *hostname = "hostname";
  const char *disableLeds = "disableLeds";
  const char *refreshLogs = "refreshLogs";
  const char *disableLedPwr = "disableLedPwr";
  const char *disableLedUSB = "disableLedUSB";
  const char *prevCoordMode = "prevCoordMode";
  const char *keepWeb = "keepWeb";
  const char *timeZoneName = "timeZoneName";
  File configFile = LittleFS.open(configFileGeneral, FILE_READ);
  DEBUG_PRINTLN(configFile.readString());
  if (!configFile)
  {
    // String deviceID = deviceModel;
    // getDeviceID(deviceID);
    DEBUG_PRINTLN("RESET ConfigGeneral");
    // String StringConfig = "{\"hostname\":\"" + deviceID + "\",\"disableLeds\": false,\"refreshLogs\":1000,\"usbMode\":0,\"disableLedPwr\":0,\"disableLedUSB\":0,\""+ coordMode +"\":0}\""+ prevCoordMode +"\":0, \"keepWeb\": 0}";
    DynamicJsonDocument doc(1024);
    doc[hostname] = deviceModel;
    doc[disableLeds] = 0;
    doc[refreshLogs] = 1000;
    doc[disableLedPwr] = 0;
    doc[disableLedUSB] = 0;
    doc[coordMode] = 0;
    doc[prevCoordMode] = 0;
    doc[keepWeb] = 0;
    writeDefaultConfig(configFileGeneral, doc);
  }

  configFile = LittleFS.open(configFileGeneral, FILE_READ);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);

  if (error)
  {
    DEBUG_PRINTLN(F("deserializeJson() failed: "));
    DEBUG_PRINTLN(error.f_str());

    configFile.close();
    LittleFS.remove(configFileGeneral);
    return false;
  }

  if ((double)doc[refreshLogs] < 1000)
  {
    ConfigSettings.refreshLogs = 1000;
  }
  else
  {
    ConfigSettings.refreshLogs = (int)doc[refreshLogs];
  }
  DEBUG_PRINTLN(F("[loadConfigGeneral] 'doc[coordMode]' res is:"));
  DEBUG_PRINTLN(String((uint8_t)doc[coordMode]));
  strlcpy(ConfigSettings.hostname, doc[hostname] | "", sizeof(ConfigSettings.hostname));
  ConfigSettings.coordinator_mode = static_cast<COORDINATOR_MODE_t>((uint8_t)doc[coordMode]);
  ConfigSettings.prevCoordinator_mode = static_cast<COORDINATOR_MODE_t>((uint8_t)doc[prevCoordMode]);
  DEBUG_PRINTLN(F("[loadConfigGeneral] 'static_cast' res is:"));
  DEBUG_PRINTLN(String(ConfigSettings.coordinator_mode));
  ConfigSettings.disableLedPwr = (uint8_t)doc[disableLedPwr];
  // DEBUG_PRINTLN(F("[loadConfigGeneral] disableLedPwr"));
  ConfigSettings.disableLedUSB = (uint8_t)doc[disableLedUSB];
  // DEBUG_PRINTLN(F("[loadConfigGeneral] disableLedUSB"));
  ConfigSettings.disableLeds = (uint8_t)doc[disableLeds];
  // DEBUG_PRINTLN(F("[loadConfigGeneral] disableLeds"));
  ConfigSettings.keepWeb = (uint8_t)doc[keepWeb];
  // DEBUG_PRINTLN(F("[loadConfigGeneral] disableLeds"));
  strlcpy(ConfigSettings.timeZone, doc[timeZoneName] | "", sizeof(ConfigSettings.timeZone));
  configFile.close();
  DEBUG_PRINTLN(F("[loadConfigGeneral] config load done"));
  return true;
}

bool loadConfigSecurity()
{
  const char *disableWeb = "disableWeb";
  const char *webAuth = "webAuth";
  const char *webUser = "webUser";
  const char *webPass = "webPass";
  const char *fwEnabled = "fwEnabled";
  const char *fwIp = "fwIp";
  File configFile = LittleFS.open(configFileSecurity, FILE_READ);
  if (!configFile)
  {
    // String StringConfig = "{\"disableWeb\":0,\"webAuth\":0,\"webUser\":"",\"webPass\":""}";
    DynamicJsonDocument doc(1024);
    doc[disableWeb] = 0;
    doc[webAuth] = 0;
    doc[webUser] = "admin";
    doc[webPass] = "";
    doc[fwEnabled] = 0;
    doc[fwIp] = "";
    writeDefaultConfig(configFileSecurity, doc);
  }

  configFile = LittleFS.open(configFileSecurity, FILE_READ);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);

  if (error)
  {
    DEBUG_PRINTLN(F("deserializeJson() failed: "));
    DEBUG_PRINTLN(error.f_str());

    configFile.close();
    LittleFS.remove(configFileSecurity);
    return false;
  }

  ConfigSettings.disableWeb = (uint8_t)doc[disableWeb];
  ConfigSettings.webAuth = (uint8_t)doc[webAuth];
  strlcpy(ConfigSettings.webUser, doc[webUser] | "", sizeof(ConfigSettings.webUser));
  strlcpy(ConfigSettings.webPass, doc[webPass] | "", sizeof(ConfigSettings.webPass));
  ConfigSettings.fwEnabled = (uint8_t)doc[fwEnabled];
  ConfigSettings.fwIp = parse_ip_address(doc[fwIp] | "0.0.0.0");

  configFile.close();
  return true;
}

bool loadConfigSerial()
{
  const char *baud = "baud";
  const char *port = "port";
  File configFile = LittleFS.open(configFileSerial, FILE_READ);
  if (!configFile)
  {
    // String StringConfig = "{\"baud\":115200,\"port\":6638}";
    DynamicJsonDocument doc(1024);
    doc[baud] = 115200;
    doc[port] = 6638;
    writeDefaultConfig(configFileSerial, doc);
  }

  configFile = LittleFS.open(configFileSerial, FILE_READ);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);

  if (error)
  {
    DEBUG_PRINTLN(F("deserializeJson() failed: "));
    DEBUG_PRINTLN(error.f_str());

    configFile.close();
    LittleFS.remove(configFileSerial);
    return false;
  }

  ConfigSettings.serialSpeed = (int)doc[baud];
  ConfigSettings.socketPort = (int)doc[port];
  if (ConfigSettings.socketPort == 0)
  {
    ConfigSettings.socketPort = TCP_LISTEN_PORT;
  }
  configFile.close();
  return true;
}

bool loadConfigMqtt()
{
  const char *enable = "enable";
  const char *server = "server";
  const char *port = "port";
  const char *user = "user";
  const char *pass = "pass";
  const char *topic = "topic";
  const char *interval = "interval";
  const char *discovery = "discovery";

  File configFile = LittleFS.open(configFileMqtt, FILE_READ);
  if (!configFile)
  {
    char deviceIdArr[20];
    getDeviceID(deviceIdArr);

    DynamicJsonDocument doc(1024);
    doc[enable] = 0;
    doc[server] = "";
    doc[port] = 1883;
    doc[user] = "mqttuser";
    doc[pass] = "";
    doc[topic] = String(deviceIdArr);
    doc[interval] = 60;
    doc[discovery] = 0;
    writeDefaultConfig(configFileMqtt, doc);
  }

  configFile = LittleFS.open(configFileMqtt, FILE_READ);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);

  if (error)
  {
    DEBUG_PRINTLN(F("deserializeJson() failed: "));
    DEBUG_PRINTLN(error.f_str());
    configFile.close();
    LittleFS.remove(configFileMqtt);
    return false;
  }

  MqttSettings.enable = (int)doc[enable];
  strlcpy(MqttSettings.server, doc[server] | "", sizeof(MqttSettings.server));
  MqttSettings.serverIP = parse_ip_address(MqttSettings.server);
  MqttSettings.port = (int)doc[port];
  strlcpy(MqttSettings.user, doc[user] | "", sizeof(MqttSettings.user));
  strlcpy(MqttSettings.pass, doc[pass] | "", sizeof(MqttSettings.pass));
  strlcpy(MqttSettings.topic, doc[topic] | "", sizeof(MqttSettings.topic));
  MqttSettings.interval = (int)doc[interval];
  MqttSettings.discovery = (int)doc[discovery];

  configFile.close();
  return true;
}

bool loadConfigWg()
{
  const char *enable = "enable";
  const char *localAddr = "localAddr";
  const char *localIP = "localIP";
  const char *endAddr = "endAddr";
  const char *endPubKey = "endPubKey";
  const char *endPort = "endPort";

  File configFile = LittleFS.open(configFileWg, FILE_READ);
  if (!configFile)
  {
    DynamicJsonDocument doc(1024);
    doc[enable] = 0;
    doc[localAddr] = "";
    doc[localIP] = "";
    doc[endAddr] = "";
    doc[endPubKey] = "";
    doc[endPort] = "";
    writeDefaultConfig(configFileWg, doc);
  }

  configFile = LittleFS.open(configFileWg, FILE_READ);
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, configFile);

  if (error)
  {
    DEBUG_PRINTLN(F("deserializeJson() failed: "));
    DEBUG_PRINTLN(error.f_str());
    configFile.close();
    LittleFS.remove(configFileWg);
    return false;
  }

  WgSettings.enable = (int)doc[enable];

  strlcpy(WgSettings.localAddr, doc[localAddr] | "", sizeof(WgSettings.localAddr));
  WgSettings.localIP = parse_ip_address(WgSettings.localAddr);

  strlcpy(WgSettings.localPrivKey, doc[localIP] | "", sizeof(WgSettings.localPrivKey));
  strlcpy(WgSettings.endAddr, doc[endAddr] | "", sizeof(WgSettings.endAddr));
  strlcpy(WgSettings.endPubKey, doc[endPubKey] | "", sizeof(WgSettings.endPubKey));
  WgSettings.endPort = (int)doc[endPort];

  configFile.close();
  return true;
}

void startAP(const bool start)
{
  Serial.print("[startAP] Called with start=");
  Serial.println(start);
  if (ConfigSettings.apStarted)
  {
    Serial.println("[startAP] AP already started");
    if (!start)
    {
      if (ConfigSettings.coordinator_mode != COORDINATOR_MODE_WIFI)
      {
        WiFi.softAPdisconnect(true); // off wifi
      }
      else
      {
        WiFi.mode(WIFI_STA);
      }
      dnsServer.stop();
      ConfigSettings.apStarted = false;
    }
  }
  else
  {
    if (!start)
      return;
    WiFi.mode(WIFI_AP_STA); // WIFI_AP_STA for possible wifi scan in wifi mode
    WiFi.disconnect();
    // String AP_NameString;
    // getDeviceID(AP_NameString);

    // char AP_NameChar[AP_NameString.length() + 1];
    // memset(AP_NameChar, 0, AP_NameString.length() + 1);

    // for (int i = 0; i < AP_NameString.length(); i++){
    //   AP_NameChar[i] = AP_NameString.charAt(i);
    // }

    Serial.println("[startAP] Starting AP mode...");
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    char apSsid[32];  // Increased to 32 to safely hold SSID + null terminator
    getDeviceID(apSsid);
    Serial.print("[startAP] SSID: ");
    Serial.println(apSsid);
    bool apResult = WiFi.softAP(apSsid); //, WIFIPASS);
    Serial.print("[startAP] softAP result: ");
    Serial.println(apResult ? "SUCCESS" : "FAILED");
    if (apResult) {
      Serial.print("[startAP] AP IP: ");
      Serial.println(WiFi.softAPIP());
    }
    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);
    WiFi.setSleep(false);
    // ConfigSettings.wifiAPenblTime = millis();
    ConfigSettings.apStarted = true;
    startServers();
  }
}

void connectWifi()
{
  static uint8_t timeout = 0;
  if (WiFi.status() == WL_IDLE_STATUS && timeout < 20)
  { // connection in progress
    DEBUG_PRINTLN(F("[connectWifi] WL_IDLE_STATUS"));
    timeout++;
    return;
  }
  else
  {
    timeout = 0;
    DEBUG_PRINTLN(F("[connectWifi] timeout"));
  }
  WiFi.persistent(false);
  if ((strlen(ConfigSettings.ssid) >= 2) && (strlen(ConfigSettings.password) >= 8))
  {
    DEBUG_PRINTLN(F("[connectWifi] Ok SSID & PASS"));
    if (ConfigSettings.apStarted)
    {
      // DEBUG_PRINTLN(F("[connectWifi] WiFi.mode(WIFI_AP_STA)"));
      // WiFi.mode(WIFI_AP_STA);
    }
    else
    {
      DEBUG_PRINTLN(F("[connectWifi] WiFi.mode(WIFI_STA)"));
      WiFi.mode(WIFI_STA);
    }
    delay(100);
    esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);

    WiFi.begin(ConfigSettings.ssid, ConfigSettings.password);
    WiFi.setSleep(false);
    DEBUG_PRINTLN(F("[connectWifi] WiFi.begin"));

    if (!ConfigSettings.dhcpWiFi)
    {
      IPAddress ip_address = parse_ip_address(ConfigSettings.ipAddressWiFi);
      IPAddress gateway_address = parse_ip_address(ConfigSettings.ipGWWiFi);
      IPAddress netmask = parse_ip_address(ConfigSettings.ipMaskWiFi);
      WiFi.config(ip_address, gateway_address, netmask);
      DEBUG_PRINTLN(F("[connectWifi] WiFi.config"));
    }
    else
    {
      DEBUG_PRINTLN(F("[connectWifi] Try DHCP"));
    }
  }
  else
  {
    DEBUG_PRINTLN(F("[connectWifi] NO SSID & PASS"));
    if (!ConfigSettings.connectedEther)
    {
      DEBUG_PRINTLN(F("[connectWifi] and LAN not connected"));
      startAP(true);
    }
    else
    {
      DEBUG_PRINTLN(F("[connectWifi] but LAN is OK"));
    }
  }
}

void mDNS_start()
{
  const char *host = "_avatto-gw90-ti";
  const char *http = "_http";
  const char *tcp = "_tcp";
  if (!MDNS.begin(ConfigSettings.hostname))
  {
    printLogMsg("Error setting up MDNS responder!");
  }
  else
  {
    printLogMsg("mDNS responder started");
    MDNS.addService(http, tcp, 80); // web
    //--zeroconf zha--
    MDNS.addService(host, tcp, ConfigSettings.socketPort);
    MDNS.addServiceTxt(host, tcp, "version", "1.0");
    MDNS.addServiceTxt(host, tcp, "radio_type", "znp");
    MDNS.addServiceTxt(host, tcp, "baud_rate", String(ConfigSettings.serialSpeed));
    MDNS.addServiceTxt(host, tcp, "data_flow_control", "software");
  }
}

IRAM_ATTR bool debounce()
{
  volatile static unsigned long lastFire = 0;
  if (millis() - lastFire < DEBOUNCE_TIME)
  { // Debounce
    return 0;
  }
  lastFire = millis();
  return 1;
}

IRAM_ATTR void btnInterrupt()
{
  if (debounce())
    btnFlag = true;
}

void setLedsDisable(bool mode, bool setup)
{
  Serial.println("\n========== LED CONTROL ==========");
  Serial.print("[LED] Mode: ");
  Serial.println(mode ? "DISABLE ALL" : "ENABLE");
  Serial.print("[LED] Setup phase: ");
  Serial.println(setup ? "Yes" : "No");
  
  if (!setup)
  {
    Serial.println("[LED] Saving LED preferences to config...");
    const char *path = configFileGeneral;
    DynamicJsonDocument doc(300);
    File configFile = LittleFS.open(path, FILE_READ);
    deserializeJson(doc, configFile);
    configFile.close();
    doc["disableLeds"] = mode;
    doc["disableLedPwr"] = mode;
    doc["disableLedUSB"] = mode;
    configFile = LittleFS.open(path, FILE_WRITE);
    serializeJson(doc, configFile);
    configFile.close();
    ConfigSettings.disableLeds = mode;
    ConfigSettings.disableLedPwr = mode;
    ConfigSettings.disableLedUSB = mode;
  }
  
  if (mode)
  {
    Serial.println("[LED] Turning OFF all LEDs");
    digitalWrite(LED_USB, LOW);
    digitalWrite(LED_PWR, LOW);
    Serial.println("[LED] LED_USB (Red): OFF");
    Serial.println("[LED] LED_PWR (Blue): OFF");
  }
  else
  {
    // Blue Power LED
    if (!ConfigSettings.disableLedPwr)
    {
      digitalWrite(LED_PWR, HIGH);
      Serial.println("[LED] LED_PWR (Blue): ON");
    }
    else
    {
      digitalWrite(LED_PWR, LOW);
      Serial.println("[LED] LED_PWR (Blue): OFF (disabled in config)");
    }
    
    // Red USB LED - only on in USB mode
    if (ConfigSettings.coordinator_mode == COORDINATOR_MODE_USB && !ConfigSettings.disableLedUSB)
    {
      digitalWrite(LED_USB, HIGH);
      Serial.println("[LED] LED_USB (Red): ON (USB mode)");
    }
    else
    {
      digitalWrite(LED_USB, LOW);
      if (ConfigSettings.coordinator_mode == COORDINATOR_MODE_USB) {
        Serial.println("[LED] LED_USB (Red): OFF (disabled in config)");
      } else {
        const char* modeNames[] = {"LAN", "WiFi", "USB"};
        Serial.print("[LED] LED_USB (Red): OFF (not in USB mode, current: ");
        Serial.print(modeNames[ConfigSettings.coordinator_mode]);
        Serial.println(")");
      }
    }
  }
  Serial.println("================================\n");
}

void handleLongBtn()
{
  if (!digitalRead(BTN))
  { // long press
    DEBUG_PRINT(F("Long press "));
    DEBUG_PRINT(btnFlag);
    DEBUG_PRINTLN(F("s"));
    if (btnFlag >= 4)
    {
      printLogMsg("Long press 4sec - zigbeeEnableBSL");
      zigbeeEnableBSL();
      tmrBtnLongPress.stop();
      btnFlag = false;
    }
    else
      btnFlag++;
  }
  else
  { // stop long press
    if (btnFlag >= 2)
    {
      printLogMsg("Long press 2sec - setLedsDisable");
      setLedsDisable(!ConfigSettings.disableLeds, false);
    }
    tmrBtnLongPress.stop();
    btnFlag = false;
    printLogMsg("Stop long press");
  }
}

void toggleUsbMode()
{
  Serial.println("\n========== MODE TOGGLE TRIGGERED ==========");
  
  const char* modeNames[] = {"LAN", "WiFi", "USB"};
  Serial.print("[MODE] Current Mode: ");
  Serial.print(modeNames[ConfigSettings.coordinator_mode]);
  Serial.print(" (");
  Serial.print(ConfigSettings.coordinator_mode);
  Serial.println(")");
  
  if (ConfigSettings.coordinator_mode != COORDINATOR_MODE_USB)
  {
    Serial.print("[MODE] Switching from ");
    Serial.print(modeNames[ConfigSettings.coordinator_mode]);
    Serial.println(" to USB mode");
    
    ConfigSettings.prevCoordinator_mode = ConfigSettings.coordinator_mode; // remember current state
    ConfigSettings.coordinator_mode = COORDINATOR_MODE_USB;                // toggle
    
    Serial.print("[MODE] Previous mode saved: ");
    Serial.println(modeNames[ConfigSettings.prevCoordinator_mode]);
  }
  else
  {
    Serial.print("[MODE] Switching from USB to ");
    Serial.print(modeNames[ConfigSettings.prevCoordinator_mode]);
    Serial.println(" mode");
    
    ConfigSettings.coordinator_mode = ConfigSettings.prevCoordinator_mode;
  }
  
  Serial.print("[MODE] New Mode: ");
  Serial.print(modeNames[ConfigSettings.coordinator_mode]);
  Serial.print(" (");
  Serial.print(ConfigSettings.coordinator_mode);
  Serial.println(")");
  
  // Save configuration
  const char *path = configFileGeneral;
  DynamicJsonDocument doc(300);
  File configFile = LittleFS.open(path, FILE_READ);
  deserializeJson(doc, configFile);
  configFile.close();
  doc[prevCoordMode] = ConfigSettings.prevCoordinator_mode;
  doc[coordMode] = ConfigSettings.coordinator_mode;
  configFile = LittleFS.open(path, FILE_WRITE);
  serializeJson(doc, configFile);
  configFile.close();
  
  Serial.println("[MODE] Configuration saved");
  Serial.println("[MODE] Restarting device...");
  Serial.println("==========================================\n");
  
  // LED will be set correctly after restart by setLedsDisable()
  ESP.restart();
}

void networkStart()
{
  // Register WiFi event handler once
  static bool eventHandlerRegistered = false;
  if (!eventHandlerRegistered) {
    WiFi.onEvent(NetworkEvent);
    eventHandlerRegistered = true;
  }
  
  // Start network overseer
  if (tmrNetworkOverseer.state() == STOPPED)
  {
    tmrNetworkOverseer.start();
  }
  
  // Initialize network based on mode
  switch (ConfigSettings.coordinator_mode)
  {
  case COORDINATOR_MODE_LAN:
    initLan();
    break;
    
  case COORDINATOR_MODE_WIFI:
    connectWifi();
    break;
    
  case COORDINATOR_MODE_USB:
    if (ConfigSettings.keepWeb)
    {
      initLan();
      connectWifi();
    }
    break;
  }
}

void setupCoordinatorMode()
{
  Serial.println("\n========== COORDINATOR MODE SETUP ==========");
  
  if (ConfigSettings.coordinator_mode > 2 || ConfigSettings.coordinator_mode < 0)
  {
    Serial.println("[ERROR] Invalid mode! Resetting to LAN");
    ConfigSettings.coordinator_mode = COORDINATOR_MODE_LAN;
  }
  
  const char* modeNames[] = {"LAN", "WiFi", "USB"};
  Serial.print("[MODE] Current: ");
  Serial.print(modeNames[ConfigSettings.coordinator_mode]);
  Serial.print(" (");
  Serial.print(ConfigSettings.coordinator_mode);
  Serial.println(")");
  
  // Set MODE_SWITCH pin based on mode
  if (ConfigSettings.coordinator_mode == COORDINATOR_MODE_USB)
  {
    Serial.println("[MODE] MODE_SWITCH=HIGH (Zigbee->USB)");
    digitalWrite(MODE_SWITCH, 1);
  }
  else
  {
    Serial.println("[MODE] MODE_SWITCH=LOW (Zigbee->Network)");
    digitalWrite(MODE_SWITCH, 0);
  }
  
  // Enable web server if needed
  if (!ConfigSettings.disableWeb && 
      (ConfigSettings.coordinator_mode != COORDINATOR_MODE_USB || ConfigSettings.keepWeb))
  {
    Serial.println("[WEB] Server enabled");
    updWeb = true;
  }
  else
  {
    Serial.println("[WEB] Server disabled");
  }
  
  Serial.println("==========================================\n");
}

// void cmd2zigbee(const HardwareSerial serial, byte cmd[], const byte size){
//   byte checksum;
//   for (byte i = 1; i < size - 1; i++){
//     checksum ^= cmd[i];
//   }
//   cmd[size] = checksum;
//   serial.write(cmd, size);
// }

// void clearS2Buffer(){
//   while (Serial2.available()){//clear buffer
//     Serial2.read();
//   }
// }

void setup()
{
  Serial.begin(115200); // todo ifdef DEBUG
  delay(500);
  Serial.println("\n\n========== DEVICE STARTING ==========");
  Serial.println("Firmware version: " VERSION);
  Serial.println("\n[DEBUG] Serial Monitor Ready");
  Serial.println("[DEBUG] Baud Rate: 115200");
  Serial.println("[DEBUG] Use Serial Monitor to view mode switching and system logs");
  Serial.println("[DEBUG] ==========================================\n");
  ConfigSettings.apStarted = false;
  ConfigSettings.serialSpeed = 115200;
  DEBUG_PRINTLN(F("Start"));
  pinMode(CC2652P_RST, OUTPUT);
  pinMode(CC2652P_FLASH, OUTPUT);
  digitalWrite(CC2652P_RST, 1);
  digitalWrite(CC2652P_FLASH, 1);
  pinMode(LED_PWR, OUTPUT);
  pinMode(LED_USB, OUTPUT);
  pinMode(BTN, INPUT);
  pinMode(MODE_SWITCH, OUTPUT);
  // Don't set MODE_SWITCH here - let setupCoordinatorMode handle it based on actual mode
  // This prevents wrong initial state
  digitalWrite(LED_PWR, 1);
  digitalWrite(LED_USB, 1);

// hard reset
#if BUILD_ENV_NAME != debug
  if (!digitalRead(BTN))
  {
    DEBUG_PRINTLN(F("[hard reset] Entering hard reset mode"));
    uint8_t counter = 0;
    while (!digitalRead(BTN))
    {
      if (counter >= 10)
      {
        resetSettings();
      }
      else
      {
        counter++;
        DEBUG_PRINTLN(counter);
        delay(200);
      }
    }
    DEBUG_PRINTLN(F("[hard reset] Btn up, exit"));
  }
#endif
  //--------------------

  // zig connection & leds testing
  Serial2.begin(115200, SERIAL_8N1, CC2652P_RXD, CC2652P_TXD); // start zigbee serial
  zbInit();
  //-----------------

  attachInterrupt(digitalPinToInterrupt(BTN), btnInterrupt, FALLING);

  if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED, "/lfs2", 10))
  {
    DEBUG_PRINTLN(F("Error with LITTLEFS"));
    return;
  }

  DEBUG_PRINTLN(F("LITTLEFS OK"));
  if (!loadSystemVar())
  {
    DEBUG_PRINTLN(F("Error load system vars"));
    const char *path = "/config";

    if (LittleFS.mkdir(path))
    {
      DEBUG_PRINTLN(F("Config dir created"));
      delay(500);
      ESP.restart();
    }
    else
    {
      DEBUG_PRINTLN(F("mkdir failed"));
    }
  }
  else
  {
    DEBUG_PRINTLN(F("System vars load OK"));
  }

  if (!loadConfigSerial())
  {
    DEBUG_PRINTLN(F("Error load config serial"));
    ESP.restart();
  }
  else
  {
    DEBUG_PRINTLN(F("Config serial load OK"));
  }

  if ((!loadConfigWifi()) || (!loadConfigEther()) || (!loadConfigGeneral()) || (!loadConfigSecurity()) || (!loadConfigMqtt()) || (!loadConfigWg()))
  {
    DEBUG_PRINTLN(F("Error load config files"));
    ESP.restart();
  }
  else
  {
    DEBUG_PRINTLN(F("Config files load OK"));
  }
  Serial.println("\n[INIT] Configuring LED initial state...");
  Serial.print("[INIT] disableLeds config: ");
  Serial.println(ConfigSettings.disableLeds ? "true" : "false");
  setLedsDisable(ConfigSettings.disableLeds, true);
  
  // First set up coordinator mode (MODE_SWITCH pin)
  setupCoordinatorMode();
  
  // Then start network
  networkStart();
  
  ConfigSettings.connectedClients = 0;

  if (MqttSettings.enable)
  {
    mqttConnectSetup();
  }

  DEBUG_PRINTLN(millis());

  Serial2.updateBaudRate(ConfigSettings.serialSpeed); // set actual speed
  printLogMsg("Setup done");

  char deviceIdArr[20];
  getDeviceID(deviceIdArr);

  DEBUG_PRINTLN(String(deviceIdArr));
  printLogMsg(String(deviceIdArr));

  // Cron.create(const_cast<char *>("0 */1 * * * *"), ledsScheduler, false);

  /*
  cron_parse_expr(cronstring, &(Alarm[id].expr), &err);
  if (err) {
    memset(&(Alarm[id].expr), 0, sizeof(Alarm[id].expr));
    return dtINVALID_ALARM_ID;
  }
  */
}

WiFiClient client[10];

void socketClientConnected(int client)
{
  if (ConfigSettings.connectedSocket[client] != true)
  {
    DEBUG_PRINT(F("Connected client "));
    DEBUG_PRINTLN(client);
    if (ConfigSettings.connectedClients == 0)
    {
      ConfigSettings.socketTime = millis();
      DEBUG_PRINT(F("Socket time "));
      DEBUG_PRINTLN(ConfigSettings.socketTime);
      mqttPublishIo("socket", "ON");
    }
    ConfigSettings.connectedSocket[client] = true;
    ConfigSettings.connectedClients++;
  }
}

void socketClientDisconnected(int client)
{
  if (ConfigSettings.connectedSocket[client] != false)
  {
    DEBUG_PRINT(F("Disconnected client "));
    DEBUG_PRINTLN(client);
    ConfigSettings.connectedSocket[client] = false;
    ConfigSettings.connectedClients--;
    if (ConfigSettings.connectedClients == 0)
    {
      ConfigSettings.socketTime = millis();
      DEBUG_PRINT(F("Socket time "));
      DEBUG_PRINTLN(ConfigSettings.socketTime);
      mqttPublishIo("socket", "OFF");
    }
  }
}

void printRecvSocket(size_t bytes_read, uint8_t net_buf[BUFFER_SIZE])
{
  char output_sprintf[2];
  if (bytes_read > 0)
  {
    String tmpTime;
    String buff = "";
    unsigned long timeLog = millis();
    tmpTime = String(timeLog, DEC);
    logPush('[');
    for (int j = 0; j < tmpTime.length(); j++)
    {
      logPush(tmpTime[j]);
    }
    logPush(']');
    logPush(' ');
    logPush('-');
    logPush('>');

    for (int i = 0; i < bytes_read; i++)
    {
      sprintf(output_sprintf, "%02x", net_buf[i]);
      logPush(' ');
      logPush(output_sprintf[0]);
      logPush(output_sprintf[1]);
    }
    logPush('\n');
  }
}

void printSendSocket(size_t bytes_read, uint8_t serial_buf[BUFFER_SIZE])
{
  char output_sprintf[2];
  String tmpTime;
  String buff = "";
  unsigned long timeLog = millis();
  tmpTime = String(timeLog, DEC);
  logPush('[');
  for (int j = 0; j < tmpTime.length(); j++)
  {
    logPush(tmpTime[j]);
  }
  logPush(']');
  logPush(' ');
  logPush('<');
  logPush('-');
  for (int i = 0; i < bytes_read; i++)
  {
    // if (serial_buf[i] == 0x01)
    //{
    // }
    sprintf(output_sprintf, "%02x", serial_buf[i]);
    logPush(' ');
    logPush(output_sprintf[0]);
    logPush(output_sprintf[1]);
    // if (serial_buf[i] == 0x03)
    // {

    //}
  }
  logPush('\n');
}

void loop(void)
{
  if (btnFlag)
  {
    if (!digitalRead(BTN))
    { // pressed
      if (tmrBtnLongPress.state() == STOPPED)
      {
        tmrBtnLongPress.start();
      }
    }
    else
    {
      if (tmrBtnLongPress.state() == RUNNING)
      {
        btnFlag = false;
        tmrBtnLongPress.stop();
        toggleUsbMode();
      }
    }
  }

  tmrBtnLongPress.update();
  tmrNetworkOverseer.update();
  if (updWeb)
  {
    webServerHandleClient();
  }
  else
  {
    if (ConfigSettings.connectedClients == 0)
    {
      webServerHandleClient();
    }
  }

  if (ConfigSettings.coordinator_mode != COORDINATOR_MODE_USB)
  {
    uint16_t net_bytes_read = 0;
    uint8_t net_buf[BUFFER_SIZE];
    uint16_t serial_bytes_read = 0;
    uint8_t serial_buf[BUFFER_SIZE];

    if (server.hasClient())
    {
      for (byte i = 0; i < MAX_SOCKET_CLIENTS; i++)
      {
        if (!client[i] || !client[i].connected())
        {
          if (client[i])
          {
            client[i].stop();
          }
          if (ConfigSettings.fwEnabled)
          {
            WiFiClient TempClient2 = server.available();
            if (TempClient2.remoteIP() == ConfigSettings.fwIp)
            {
              printLogMsg(String("[SOCK IP WHITELIST] Accepted connection from IP: ") + TempClient2.remoteIP().toString());
              client[i] = TempClient2;
              continue;
            }
            else
            {
              printLogMsg(String("[SOCK IP WHITELIST] Rejected connection from unknown IP: ") + TempClient2.remoteIP().toString());
            }
          }
          else
          {
            client[i] = server.available();
            continue;
          }
        }
      }
      WiFiClient TempClient = server.available();
      TempClient.stop();
    }

    for (byte cln = 0; cln < MAX_SOCKET_CLIENTS; cln++)
    {
      if (client[cln])
      {
        socketClientConnected(cln);
        while (client[cln].available())
        { // read from LAN
          net_buf[net_bytes_read] = client[cln].read();
          if (net_bytes_read < BUFFER_SIZE - 1)
            net_bytes_read++;
        } // send to Zigbee
        Serial2.write(net_buf, net_bytes_read);
        // print to web console
        printRecvSocket(net_bytes_read, net_buf);
        net_bytes_read = 0;
      }
      else
      {
        socketClientDisconnected(cln);
      }
    }

    if (Serial2.available())
    {
      while (Serial2.available())
      { // read from Zigbee
        serial_buf[serial_bytes_read] = Serial2.read();
        if (serial_bytes_read < BUFFER_SIZE - 1)
          serial_bytes_read++;
      }
      // send to LAN
      for (byte cln = 0; cln < MAX_SOCKET_CLIENTS; cln++)
      {
        if (client[cln])
          client[cln].write(serial_buf, serial_bytes_read);
      }
      // print to web console
      printSendSocket(serial_bytes_read, serial_buf);
      serial_bytes_read = 0;
    }

    if (MqttSettings.enable)
    {
      mqttLoop();
    }
  }

  if (WiFi.getMode() == WIFI_MODE_AP || WiFi.getMode() == WIFI_MODE_APSTA)
  {
    dnsServer.processNextRequest();
  }
  Cron.delay();
}
