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



#include <freertos/FreeRTOS.h>
#include <freertos/task.h>




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


const byte ZIGBEE_LED_ON[] = {0xFE, 0x02, 0x27, 0x0A, 0x01, 0x01, 0x2F};
const byte ZIGBEE_LED_OFF[] = {0xFE, 0x02, 0x27, 0x0A, 0x01, 0x00, 0x2E};
const byte cmdSysVersion[] = {0Xfe, 0, 0X21, 0X02, 0x23};
const byte cmdGetChipId[] = {3, 0x28, 0X28};


void factory_test(void);


void initLan()
{
  Serial.println(F("\n[LAN] ======== initLan() START ========"));
  Serial.print(F("[LAN] ETH_ADDR=")); Serial.print(ETH_ADDR_1);
  Serial.print(F(" PWR_PIN=")); Serial.print(ETH_POWER_PIN_1);
  Serial.print(F(" MDC=")); Serial.print(ETH_MDC_PIN_1);
  Serial.print(F(" MDIO=")); Serial.print(ETH_MDIO_PIN_1);
  Serial.print(F(" PWR_ALT=")); Serial.println(ETH_POWER_PIN_ALTERNATIVE_1);
  Serial.print(F("[LAN] DHCP=")); Serial.println(ConfigSettings.dhcp);
  if (!ConfigSettings.dhcp) {
    Serial.print(F("[LAN] Static IP=")); Serial.println(ConfigSettings.ipAddress);
    Serial.print(F("[LAN] Static GW=")); Serial.println(ConfigSettings.ipGW);
    Serial.print(F("[LAN] Static Mask=")); Serial.println(ConfigSettings.ipMask);
  }

  // Hardware reset LAN8720 PHY via GPIO5 to ensure clean state after ESP.restart()
  pinMode(ETH_POWER_PIN_ALTERNATIVE_1, OUTPUT);
  digitalWrite(ETH_POWER_PIN_ALTERNATIVE_1, LOW);
  delay(50);
  digitalWrite(ETH_POWER_PIN_ALTERNATIVE_1, HIGH);
  delay(300);

  if (ETH.begin(ETH_ADDR_1, ETH_POWER_PIN_1, ETH_MDC_PIN_1, ETH_MDIO_PIN_1, ETH_TYPE_1, ETH_CLK_MODE_1, ETH_POWER_PIN_ALTERNATIVE_1))
  {
    Serial.println(F("[LAN] ETH.begin() SUCCESS"));
    if (!ConfigSettings.dhcp)
    {
      Serial.println(F("[LAN] Configuring static IP..."));
      ETH.config(parse_ip_address(ConfigSettings.ipAddress), parse_ip_address(ConfigSettings.ipGW), parse_ip_address(ConfigSettings.ipMask));
      Serial.println(F("[LAN] Static IP configured"));
    }
    else
    {
      Serial.println(F("[LAN] Using DHCP"));
    }
  }
  else
  {
    Serial.println(F("[LAN] ETH.begin() FAILED!"));
  }
  Serial.println(F("[LAN] ======== initLan() END ========\n"));
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
  Serial.print(F("[SRV] startServers() called, usb=")); Serial.println(usb);
  Serial.print(F("[SRV] connectedEther=")); Serial.println(ConfigSettings.connectedEther);
  Serial.print(F("[SRV] WiFi.isConnected=")); Serial.println(WiFi.isConnected());
  Serial.print(F("[SRV] apStarted=")); Serial.println(ConfigSettings.apStarted);
  initWebServer();
  if (!usb)
    startSocketServer();
  startAP(false);
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
      startServers();
      tmrNetworkOverseer.stop();
    }
    else
    {
      if (tmrNetworkOverseer.counter() > overseerMaxRetry)
      {
        DEBUG_PRINTLN(F("WIFI counter overflow"));
        startAP(true);
        connectWifi();
      }
    }
    break;
  case COORDINATOR_MODE_LAN:
    Serial.print(F("[OVERSEER] LAN check: connectedEther="));
    Serial.print(ConfigSettings.connectedEther);
    Serial.print(F(" counter="));
    Serial.println(tmrNetworkOverseer.counter());
    if (ConfigSettings.connectedEther)
    {
      DEBUG_PRINTLN(F("LAN CONNECTED"));
      Serial.println(F("[OVERSEER] LAN connected! Starting servers..."));
      startServers();
      tmrNetworkOverseer.stop();
    }
    else
    {
      if (tmrNetworkOverseer.counter() > overseerMaxRetry)
      {
        DEBUG_PRINTLN(F("LAN counter overflow"));
        Serial.print(F("[OVERSEER] LAN timeout! counter="));
        Serial.print(tmrNetworkOverseer.counter());
        Serial.print(F(" > maxRetry="));
        Serial.println(overseerMaxRetry);
        Serial.println(F("[OVERSEER] Starting AP as fallback..."));
        startAP(true);
      }
    }
    break;
  case COORDINATOR_MODE_USB:
    if (tmrNetworkOverseer.counter() > 3)
    { // 10 seconds for wifi connect
      if (WiFi.isConnected())
      {
        tmrNetworkOverseer.stop();
        startServers(true);
      }
      else
      {
        initLan();
        if (tmrNetworkOverseer.counter() > 6)
        { // 3sec for lan
          if (ConfigSettings.connectedEther)
          {
            tmrNetworkOverseer.stop();
            startServers(true);
          }
          else
          {                            // no network interfaces
            tmrNetworkOverseer.stop(); // stop timer
            startAP(true);
          }
        }
      }
    }
    break;

  default:
    break;
  }
}

void NetworkEvent(WiFiEvent_t event)
{
  Serial.print(F("[NET_EVENT] Event ID: "));
  Serial.println(event);
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    Serial.println(F("[ETH_EVENT] ETH Started"));
    Serial.print(F("[ETH_EVENT] Setting hostname: ")); Serial.println(ConfigSettings.hostname);
    ETH.setHostname(ConfigSettings.hostname);
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    Serial.println(F("[ETH_EVENT] ETH Connected (link up)"));
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    Serial.println(F("[ETH_EVENT] ======== ETH GOT IP ========"));
    Serial.print(F("[ETH_EVENT] MAC: ")); Serial.println(ETH.macAddress());
    Serial.print(F("[ETH_EVENT] IPv4: ")); Serial.println(ETH.localIP());
    Serial.print(F("[ETH_EVENT] Subnet: ")); Serial.println(ETH.subnetMask());
    Serial.print(F("[ETH_EVENT] Gateway: ")); Serial.println(ETH.gatewayIP());
    if (ETH.fullDuplex())
    {
      Serial.println(F("[ETH_EVENT] FULL_DUPLEX"));
    }
    Serial.print(F("[ETH_EVENT] Speed: ")); Serial.print(ETH.linkSpeed()); Serial.println(F("Mbps"));
    ConfigSettings.connectedEther = true;
    Serial.println(F("[ETH_EVENT] connectedEther = true"));
    Serial.println(F("[ETH_EVENT] ========================"));
    setClock();
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED: // 21:  //SYSTEM_EVENT_ETH_DISCONNECTED:
    Serial.println(F("[ETH_EVENT] ETH Disconnected"));
    Serial.print(F("[ETH_EVENT] coordinator_mode=")); Serial.println(ConfigSettings.coordinator_mode);
    ConfigSettings.connectedEther = false;
    if (tmrNetworkOverseer.state() == STOPPED && ConfigSettings.coordinator_mode == COORDINATOR_MODE_LAN)
    {
      Serial.println(F("[ETH_EVENT] Restarting overseer for LAN mode"));
      tmrNetworkOverseer.start();
    }
    break;
  case SYSTEM_EVENT_ETH_STOP:
  case ARDUINO_EVENT_ETH_STOP:
    Serial.println(F("[ETH_EVENT] ETH Stopped"));
    ConfigSettings.connectedEther = false;
    if (tmrNetworkOverseer.state() == STOPPED)
    {
      Serial.println(F("[ETH_EVENT] Restarting overseer after ETH stop"));
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
//加载系统配置
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
    return true;
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
//加载wifi配置
bool loadConfigWifi()
{
 // File configFile = LittleFS.open(configFileWifi, FILE_READ);
  const char *enableWiFi = "enableWiFi";
  const char *ssid = "ssid";
  const char *pass = "pass";
  const char *dhcpWiFi = "dhcpWiFi";
  const char *ip = "ip";
  const char *mask = "mask";
  const char *gw = "gw";
  if (!LittleFS.exists(configFileWifi))
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
  File configFile = LittleFS.open(configFileWifi, FILE_READ);
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
//加载ethernet配置
bool loadConfigEther()
{
  const char *dhcp = "dhcp";
  const char *ip = "ip";
  const char *mask = "mask";
  const char *gw = "gw";
  //File configFile = LittleFS.open(configFileEther, FILE_READ);
  if (!LittleFS.exists(configFileEther))
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

  File configFile = LittleFS.open(configFileEther, FILE_READ);
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
//加载通用配置
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
 // File configFile = LittleFS.open(configFileGeneral, FILE_READ);
 // DEBUG_PRINTLN(configFile.readString());
  if (!LittleFS.exists(configFileGeneral))
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

  File configFile = LittleFS.open(configFileGeneral, FILE_READ);
  DEBUG_PRINTLN(configFile.readString());
  configFile.seek(0, SeekSet);
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
//加载安全配置
bool loadConfigSecurity()
{
  const char *disableWeb = "disableWeb";
  const char *webAuth = "webAuth";
  const char *webUser = "webUser";
  const char *webPass = "webPass";
  const char *fwEnabled = "fwEnabled";
  const char *fwIp = "fwIp";
  //File configFile = LittleFS.open(configFileSecurity, FILE_READ);
  if (!LittleFS.exists(configFileSecurity))
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

  File configFile = LittleFS.open(configFileSecurity, FILE_READ);
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
  //File configFile = LittleFS.open(configFileSerial, FILE_READ);
  if (!LittleFS.exists(configFileSerial))
  {
    // String StringConfig = "{\"baud\":115200,\"port\":6638}";
    DynamicJsonDocument doc(1024);
    doc[baud] = 115200;
    doc[port] = 6638;
    writeDefaultConfig(configFileSerial, doc);
  }

  File configFile = LittleFS.open(configFileSerial, FILE_READ);
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
//加载mqtt配置
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

  //File configFile = LittleFS.open(configFileMqtt, FILE_READ);
  if (!LittleFS.exists(configFileMqtt))
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

  File configFile = LittleFS.open(configFileMqtt, FILE_READ);
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
//加载wg配置
bool loadConfigWg()
{
  const char *enable = "enable";
  const char *localAddr = "localAddr";
  const char *localIP = "localIP";
  const char *endAddr = "endAddr";
  const char *endPubKey = "endPubKey";
  const char *endPort = "endPort";

  //File configFile = LittleFS.open(configFileWg, FILE_READ);
  if (!LittleFS.exists(configFileWg))
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

  File configFile = LittleFS.open(configFileWg, FILE_READ);
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
//打开关闭AP
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
        WiFi.softAPdisconnect(true);    // off wifi
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
    startServers();
    ConfigSettings.apStarted = true;
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
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11B);
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
    if (!(ConfigSettings.coordinator_mode == COORDINATOR_MODE_USB && ConfigSettings.keepWeb))
    { // dont start ap in keepWeb
      DEBUG_PRINTLN(F("[connectWifi] NO SSID & PASS"));
      startAP(true);
      DEBUG_PRINTLN(F("[connectWifi] setupWifiAP"));
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
  
  digitalWrite(LED_USB, ConfigSettings.coordinator_mode == COORDINATOR_MODE_USB ? 1 : 0);
  ESP.restart();
}

void setupCoordinatorMode()
{
  Serial.println(F("\n======== setupCoordinatorMode() ========"));
  if (ConfigSettings.coordinator_mode > 2 || ConfigSettings.coordinator_mode < 0)
  {
    Serial.println(F("[MODE] WRONG MODE DETECTED, set to LAN"));
    ConfigSettings.coordinator_mode = COORDINATOR_MODE_LAN;
  }
  const char* modeNames[] = {"LAN", "WiFi", "USB"};
  Serial.print(F("[MODE] Mode: ")); Serial.print(modeNames[ConfigSettings.coordinator_mode]);
  Serial.print(F(" (")); Serial.print(ConfigSettings.coordinator_mode); Serial.println(F(")"));
  Serial.print(F("[MODE] keepWeb=")); Serial.println(ConfigSettings.keepWeb);
  Serial.print(F("[MODE] disableWeb=")); Serial.println(ConfigSettings.disableWeb);
  if (ConfigSettings.coordinator_mode != COORDINATOR_MODE_USB || ConfigSettings.keepWeb)
  { // start network overseer
    Serial.println(F("[MODE] Registering WiFi event & starting overseer timer"));
    if (tmrNetworkOverseer.state() == STOPPED)
    {
      tmrNetworkOverseer.start();
      Serial.println(F("[MODE] tmrNetworkOverseer started"));
    }
    WiFi.onEvent(NetworkEvent);
    Serial.println(F("[MODE] NetworkEvent registered"));
  }
  else
  {
    Serial.println(F("[MODE] USB mode without keepWeb - no network overseer"));
  }
  switch (ConfigSettings.coordinator_mode)
  {
  case COORDINATOR_MODE_USB:
    Serial.println(F("[MODE] -> USB: MODE_SWITCH=HIGH"));
    digitalWrite(MODE_SWITCH, 1);
    break;

  case COORDINATOR_MODE_WIFI:
    Serial.println(F("[MODE] -> WIFI: calling connectWifi()"));
    connectWifi();
    break;

  case COORDINATOR_MODE_LAN:
    Serial.println(F("[MODE] -> LAN: calling initLan()"));
    initLan();
    break;

  default:
    break;
  }
  if (!ConfigSettings.disableWeb && (ConfigSettings.coordinator_mode != COORDINATOR_MODE_USB || ConfigSettings.keepWeb))
  {
    updWeb = true;
    Serial.println(F("[MODE] Web server enabled (updWeb=true)"));
  }
  if (ConfigSettings.coordinator_mode == COORDINATOR_MODE_USB && ConfigSettings.keepWeb)
  {
    Serial.println(F("[MODE] USB+keepWeb: trying connectWifi()"));
    connectWifi();
  }
  Serial.println(F("======== setupCoordinatorMode() END ========\n"));
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
  digitalWrite(MODE_SWITCH, 0); // enable zigbee serial
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

  zbInit();
  if(digitalRead(BTN) && ConfigSettings.coordinator_mode != COORDINATOR_MODE_USB)
  {
    factory_test();
  }
  //-----------------
  attachInterrupt(digitalPinToInterrupt(BTN), btnInterrupt, FALLING);

  Serial.println("\n[INIT] Configuring LED initial state...");
  Serial.print("[INIT] disableLeds config: ");
  Serial.println(ConfigSettings.disableLeds ? "true" : "false");
  setLedsDisable(ConfigSettings.disableLeds, true);
  setupCoordinatorMode();
  ConfigSettings.connectedClients = 0;

  if (MqttSettings.enable)
  {
    mqttConnectSetup();
  }

  DEBUG_PRINTLN(millis());

  Serial2.updateBaudRate(ConfigSettings.serialSpeed); // set actual speed

  printf("[INIT] Serial speed: %d\n",ConfigSettings.serialSpeed);

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






/***************************************************************** */
#if 1

TaskHandle_t xTestTaskHandle = NULL;

uint8_t typecRxBuf[256]={0};
uint8_t zigbeeRxBuf[256]={0};
uint8_t bsl_flag=0;


void typecReveivePktProcess(void)
{
  if (Serial.available())
  {
    uint16_t bytes_read = 0;
    
    // 读取 Serial 接收到的数据
    while (Serial.available() && bytes_read < 256 - 1)
    {
        typecRxBuf[bytes_read] = Serial.read();
        bytes_read++;
    }
    
    // 将接收到的数据通过 Serial 发送出去
    if (bytes_read > 0)
    {
        //for test
        // Serial.write(typecRxBuf, bytes_read);
        // Serial.flush();

        if(!bsl_flag)
        {
          Serial2.updateBaudRate(500000);
          zbBegin();
          bsl_flag =1;
        }
        else
        {
            Serial.printf("zigbee sync: %d",zbCheckLastCmd());
          //runFlash("/config/fw.hex");
          //zbFlashStart(0,0);
          // uint8_t status=0;
          // if(zbCheckLastCmd())
          // {
          //     Serial.println("last cmd executed success");
          // }
          //zbFlashStart(0,18000);
        }
        //runFlash();

        //zbFlashStart(0,18000);
    }
  }
}


void zigbeeReveivePktProcess(void)
{
  if (Serial2.available())
  {
    uint16_t bytes_read = 0;
    
    // 读取 Serial 接收到的数据
    while (Serial2.available() && bytes_read < 256 - 1)
    {
        zigbeeRxBuf[bytes_read] = Serial2.read();
        bytes_read++;
    }
    
    // 将接收到的数据通过 Serial 发送出去
    if (bytes_read > 0)
    {
        //for test
        //if(bsl_flag)zbSendAck();
        Serial.write(zigbeeRxBuf, bytes_read);
    }
  }
}


void factory_initLan()
{
  Serial.println(F("\n[LAN] ======== initLan() START ========"));
  Serial.print(F("[LAN] ETH_ADDR=")); Serial.print(ETH_ADDR_1);
  Serial.print(F(" PWR_PIN=")); Serial.print(ETH_POWER_PIN_1);
  Serial.print(F(" MDC=")); Serial.print(ETH_MDC_PIN_1);
  Serial.print(F(" MDIO=")); Serial.print(ETH_MDIO_PIN_1);
  Serial.print(F(" PWR_ALT=")); Serial.println(ETH_POWER_PIN_ALTERNATIVE_1);
  Serial.print(F("[LAN] DHCP=")); Serial.println(ConfigSettings.dhcp);

  // Hardware reset LAN8720 PHY via GPIO5 to ensure clean state after ESP.restart()
  pinMode(ETH_POWER_PIN_ALTERNATIVE_1, OUTPUT);
  digitalWrite(ETH_POWER_PIN_ALTERNATIVE_1, LOW);
  delay(50);
  digitalWrite(ETH_POWER_PIN_ALTERNATIVE_1, HIGH);
  delay(300);

  if (ETH.begin(ETH_ADDR_1, ETH_POWER_PIN_1, ETH_MDC_PIN_1, ETH_MDIO_PIN_1, ETH_TYPE_1, ETH_CLK_MODE_1, ETH_POWER_PIN_ALTERNATIVE_1))
  {
    Serial.println(F("[LAN] ETH.begin() SUCCESS"));
    Serial.println(F("[LAN] Using DHCP"));
  }
  else
  {
    Serial.println(F("[LAN] ETH.begin() FAILED!"));
  }
  Serial.println(F("[LAN] ======== initLan() END ========\n"));
}


void testTaskFunction(void *pvParameters)
{
  Serial.println("[TEST TASK] Test task started!");

  // ========== 连接 WiFi AP ==========
  Serial.println("\n[TEST TASK] ========== Connecting to WiFi AP ==========");
  
  const char* testSSID = "ezsmart_factory_test";
  const char* testPASS = "ezsmart@2026";  // 如果 AP 有密码，在这里设置
  //关指示灯
  digitalWrite(LED_PWR, 0);
  digitalWrite(LED_USB, 1);

  // 设置 WiFi 模式为 STA
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.print("[WIFI CONNECT] Connecting to SSID: ");
  Serial.println(testSSID);
  
  WiFi.begin(testSSID, testPASS);
  
  // 等待连接，最多尝试 20 次
  uint8_t connectAttempts = 0;
  bool wifiConnected = false;
  
  while (connectAttempts < 20)
  {
    delay(500);
    connectAttempts++;
    Serial.print("[WIFI CONNECT] Attempt ");
    Serial.print(connectAttempts);
    Serial.print(" - Status: ");
    
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("CONNECTED");
      Serial.print("[WIFI CONNECT] Local IP: ");
      Serial.println(WiFi.localIP());
      
      // 获取连接后的实时 RSSI 值
      int32_t currentRSSI = WiFi.RSSI();
      Serial.print("[WIFI CONNECT] RSSI: ");
      Serial.print(currentRSSI);
      Serial.println(" dBm");
      
      // 根据信号强度判断连接质量
      if (currentRSSI >= -50)
      {
        Serial.println("[TEST TASK] Signal: EXCELLENT");
      }
      else if (currentRSSI >= -60)
      {
        Serial.println("[TEST TASK] Signal: GOOD");
      }
      else if (currentRSSI >= -70)
      {
        Serial.println("[TEST TASK] Signal: FAIR");
      }
      else
      {
        Serial.println("[TEST TASK] Signal: WEAK");
      }
      
      wifiConnected = true;
      break;
    }
    else
    {
      Serial.print("Connecting... (");
      Serial.print(WiFi.status());
      Serial.println(")");
    }
  }
  
  if (!wifiConnected)
  {
    Serial.println("[WIFI CONNECT] Failed to connect to AP!");
    Serial.print("[WIFI CONNECT] Final Status: ");
    Serial.println(WiFi.status());
  }
  
  // ========== WiFi 网络测试 ==========
  Serial.println("\n[TEST TASK] ========== WIFI NETWORK TEST START ==========");
  
  uint32_t wifiTestCounter = 0;
  uint32_t wifiSuccessCount = 0;
  uint32_t wifiFailCount = 0;
  
  // 先进行 10 次 WiFi 测试（约 50 秒）
  while (wifiTestCounter < 3)
  {
    wifiTestCounter++;
    
    Serial.println("\n========================================");
    Serial.println("         WIFI TEST                      ");
    Serial.println("========================================");
    Serial.print("[WIFI TEST] Test Count: ");
    Serial.println(wifiTestCounter);
    
    bool wifiTestConnected = false;
    
    // 1. 检测 WiFi 连接状态
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("[WIFI TEST] WiFi Status: CONNECTED");
      Serial.print("[WIFI TEST] SSID: ");
      Serial.println(WiFi.SSID());
      Serial.print("[WIFI TEST] Local IP: ");
      Serial.println(WiFi.localIP());
      Serial.print("[WIFI TEST] Subnet Mask: ");
      Serial.println(WiFi.subnetMask());
      Serial.print("[WIFI TEST] Gateway: ");
      Serial.println(WiFi.gatewayIP());
      Serial.print("[WIFI TEST] DNS Server: ");
      Serial.println(WiFi.dnsIP());
      
      // 获取实时 RSSI 值
      int32_t rssi = WiFi.RSSI();
      Serial.print("[WIFI TEST] Signal Strength (RSSI): ");
      Serial.print(rssi);
      Serial.println(" dBm");
      
      wifiTestConnected = true;
      wifiSuccessCount++;
    }
    else
    {
      Serial.print("[WIFI TEST] WiFi Status: DISCONNECTED (");
      Serial.print(WiFi.status());
      Serial.println(")");
      
      // 尝试重新连接
      Serial.println("[WIFI TEST] Attempting to reconnect...");
      WiFi.reconnect();
      delay(2000);
      
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.println("[WIFI TEST] Reconnect SUCCESS");
        Serial.print("[WIFI TEST] RSSI: ");
        Serial.print(WiFi.RSSI());
        Serial.println(" dBm");
        wifiTestConnected = true;
        wifiSuccessCount++;
      }
      else
      {
        Serial.println("[WIFI TEST] Reconnect FAILED");
        wifiFailCount++;
      }
    }
    
    // 2. WiFi 网络连通性测试
    if (wifiTestConnected)
    {
      Serial.println("[WIFI TEST] Testing WiFi network connectivity...");
      
      WiFiClient wifiTestClient;
      const char* testHost = "www.baidu.com";
      const int testPort = 80;
      
      unsigned long startTime = millis();
      int connectResult = wifiTestClient.connect(testHost, testPort);
      unsigned long connectTime = millis() - startTime;
      
      if (connectResult)
      {
        Serial.print("[WIFI TEST] DNS Resolution: SUCCESS (");
        Serial.print(wifiTestClient.remoteIP());
        Serial.print(") | Connect Time: ");
        Serial.print(connectTime);
        Serial.println(" ms");
        Serial.print("[WIFI TEST] Used Interface: WiFi (");
        Serial.print(WiFi.localIP());
        Serial.println(")");
        wifiTestClient.stop();
      }
      else
      {
        Serial.println("[WIFI TEST] DNS Resolution: FAILED");
        Serial.println("[WIFI TEST] Possible causes:");
        Serial.println("[WIFI TEST]   - DNS server not configured");
        Serial.println("[WIFI TEST]   - Gateway unreachable");
        Serial.println("[WIFI TEST]   - No Internet access");
      }
    }
    
    // 统计信息
    Serial.println("\n[WIFI TEST] ========== Statistics ==========");
    Serial.print("[WIFI TEST]   Total Checks: ");
    Serial.println(wifiTestCounter);
    Serial.print("[WIFI TEST]   Success Count: ");
    Serial.println(wifiSuccessCount);
    Serial.print("[WIFI TEST]   Fail Count: ");
    Serial.println(wifiFailCount);
    
    if (wifiTestCounter > 0)
    {
      float wifiRate = (float)wifiSuccessCount / wifiTestCounter * 100.0f;
      Serial.print("[WIFI TEST]   Success Rate: ");
      Serial.print(wifiRate);
      Serial.println("%");
    }
    
    Serial.println("========================================\n");
  }

  // ========== LAN 网络测试 ==========
  Serial.println("\n[TEST TASK] ========== LAN NETWORK TEST START ==========");
  
  uint32_t lanTestCounter = 0;
  uint32_t lanSuccessCount = 0;
  uint32_t lanFailCount = 0;
  
  factory_initLan();
  delay(1000);
  // 进行 10 次 LAN 测试（约 50 秒）
  while (lanTestCounter < 3)
  {
    lanTestCounter++;
    
    Serial.println("\n========================================");
    Serial.println("         LAN TEST                       ");
    Serial.println("========================================");
    Serial.print("[LAN TEST] Test Count: ");
    Serial.println(lanTestCounter);
    
    bool lanConnected = false;
    
    // 1. 检测物理连接
     if (ETH.linkUp())
    {
        Serial.println("[LAN TEST] Physical Link: UP");
        Serial.print("[LAN TEST] Link Speed: ");
        Serial.print(ETH.linkSpeed());
        Serial.println(" Mbps");
        
        // 检查 IP 配置
        if (ETH.localIP() != IPAddress(0, 0, 0, 0))
        {
            Serial.println("[LAN TEST] ETH IP Status: VALID");
            Serial.print("[LAN TEST] Local IP: ");
            Serial.println(ETH.localIP());
            Serial.print("[LAN TEST] Subnet Mask: ");
            Serial.println(ETH.subnetMask());
            Serial.print("[LAN TEST] Gateway: ");
            Serial.println(ETH.gatewayIP());
            lanConnected = true;
            lanSuccessCount++;
        }
        else
        {
            Serial.println("[LAN TEST] ETH IP Status: INVALID");
            lanFailCount++;
        }
    }
    else
    {
        Serial.println("[LAN TEST] Physical Link: DOWN");
        Serial.println("[LAN TEST] Check Ethernet cable connection!");
        lanFailCount++;
    }
    
    // 3. LAN 网络连通性测试
    if (lanConnected)
    {
      Serial.println("[LAN TEST] Testing LAN network connectivity...");
      
      WiFiClient lanTestClient;
      const char* testHost = "www.baidu.com";
      const int testPort = 80;
      
      unsigned long startTime = millis();
      int connectResult = lanTestClient.connect(testHost, testPort);
      unsigned long connectTime = millis() - startTime;
      
      if (connectResult)
      {
        Serial.print("[LAN TEST] DNS Resolution: SUCCESS (");
        Serial.print(lanTestClient.remoteIP());
        Serial.print(") | Connect Time: ");
        Serial.print(connectTime);
        Serial.println(" ms");
        Serial.print("[LAN TEST] Used Interface: ETH (");
        Serial.print(ETH.localIP());
        Serial.println(")");
        lanTestClient.stop();
      }
      else
      {
        Serial.println("[LAN TEST] DNS Resolution: FAILED");
        Serial.println("[LAN TEST] Possible causes:");
        Serial.println("[LAN TEST]   - DNS server not configured");
        Serial.println("[LAN TEST]   - Gateway unreachable");
        Serial.println("[LAN TEST]   - No Internet access");
      }
    }
    
    // 统计信息
    Serial.println("\n[LAN TEST] ========== Statistics ==========");
    Serial.print("[LAN TEST]   Total Checks: ");
    Serial.println(lanTestCounter);
    Serial.print("[LAN TEST]   Success Count: ");
    Serial.println(lanSuccessCount);
    Serial.print("[LAN TEST]   Fail Count: ");
    Serial.println(lanFailCount);
    
    if (lanTestCounter > 0)
    {
      float lanRate = (float)lanSuccessCount / lanTestCounter * 100.0f;
      Serial.print("[LAN TEST]   Success Rate: ");
      Serial.print(lanRate);
      Serial.println("%");
    }
    
    Serial.println("========================================\n");
  }
  
  // ========== 最终汇总报告 ==========
  Serial.println("\n########################################################");
  Serial.println("#              FACTORY TEST FINAL REPORT              #");
  Serial.println("########################################################");
  
  Serial.println("\n[WIFI SUMMARY]");
  Serial.print("  Total Tests: ");
  Serial.println(wifiTestCounter);
  Serial.print("  Success: ");
  Serial.println(wifiSuccessCount);
  Serial.print("  Failed: ");
  Serial.println(wifiFailCount);
  if (wifiTestCounter > 0)
  {
    float wifiRate = (float)wifiSuccessCount / wifiTestCounter * 100.0f;
    Serial.print("  Success Rate: ");
    Serial.print(wifiRate);
    Serial.println("%");
  }
  
  Serial.println("\n[LAN SUMMARY]");
  Serial.print("  Total Tests: ");
  Serial.println(lanTestCounter);
  Serial.print("  Success: ");
  Serial.println(lanSuccessCount);
  Serial.print("  Failed: ");
  Serial.println(lanFailCount);
  if (lanTestCounter > 0)
  {
    float lanRate = (float)lanSuccessCount / lanTestCounter * 100.0f;
    Serial.print("  Success Rate: ");
    Serial.print(lanRate);
    Serial.println("%");
  }
  
  Serial.println("\n########################################################");
  Serial.println("#                  TEST COMPLETED                       #");
  Serial.println("########################################################\n");
  

  //打开ZIGBEE指示灯
  Serial2.write(ZIGBEE_LED_ON, sizeof(ZIGBEE_LED_ON));      //D11亮
  //打开wifi指示灯
  if(wifiSuccessCount)digitalWrite(LED_PWR, 1);             //D9亮
  //打开LAN指示灯
  if(lanSuccessCount)digitalWrite(LED_USB, 0);              //D12亮 

  // 测试完成后进入空闲循环
  while (1)
  {
    if(!digitalRead(BTN))
    {
      digitalWrite(LED_USB, 1);
      vTaskDelay(500 / portTICK_PERIOD_MS);
      digitalWrite(LED_USB, 0);
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

void factory_test(void)
{
  // 扫描 WiFi 热点
  Serial.println("[FACTORY] Scanning WiFi networks...");

  WiFi.mode(WIFI_STA);
  int n = WiFi.scanNetworks();  
  if (n < 0)
  {
      Serial.println("[FACTORY] WiFi scan FAILED!");
      Serial.print("[FACTORY] Error code: ");
      Serial.println(n);
      return;
  }
  else if (n == 0)
  {
      Serial.println("[FACTORY] No AP found");
      WiFi.scanDelete();
      return;
  }
    
  Serial.print("[FACTORY] Total APs found: ");
  Serial.println(n);

  bool foundTestAP = false;
  int32_t testAP_RSSI = -100;
    
    // ✅ 手动过滤 SSID
  for (int i = 0; i < n; i++)
  {
      String ssid = WiFi.SSID(i);
      int32_t rssi = WiFi.RSSI(i);
      
      Serial.print("[FACTORY] SSID[");
      Serial.print(i);
      Serial.print("]: ");
      Serial.print(ssid);
      Serial.print(" | RSSI: ");
      Serial.print(rssi);
      Serial.println(" dBm");
      
      if (ssid == "ezsmart_factory_test")
      {
          foundTestAP = true;
          if (rssi > testAP_RSSI)
          {
              testAP_RSSI = rssi;  // 选择信号最好的
          }
          Serial.println("[FACTORY] >>> Found target AP!");
      }
  }

  // 释放扫描结果内存
  WiFi.scanDelete();

  if (foundTestAP)
  {
    // 将 RSSI 值作为参数传递给任务（通过指针转换）
    xTaskCreate(
        testTaskFunction,
        "TestTask",
        4096,
        (void *)(intptr_t)testAP_RSSI,  // 传递 RSSI 值
        5,
        &xTestTaskHandle
    );
    Serial.println("[FACTORY] Test task created successfully");
    Serial.println("[FACTORY] Entering factory test mode...");
    
    while(1)
    {
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
  else
  {
    Serial.println("[FACTORY] 'ez_factory_test' AP not found, continuing normal startup");
  }
}

#endif












