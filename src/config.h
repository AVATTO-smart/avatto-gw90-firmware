#ifndef CONFIG_H_
#define CONFIG_H_

#include <Arduino.h>
#include <CircularBuffer.hpp>
#include "version.h"

// #define DEBUG
// ESP32 PINS TO CONTROL LAN8720
#define ETH_CLK_MODE_1                ETH_CLOCK_GPIO17_OUT       // 8720时钟引脚：GPIO17输出时钟
#define ETH_POWER_PIN_ALTERNATIVE_1   5                          // 8720复位引脚
#define ETH_POWER_PIN_1               -1                         // 未用
#define ETH_TYPE_1                    ETH_PHY_LAN8720            // 8720 PHY类型
#define ETH_ADDR_1                    0                          // 8720 PHY地址：悬空，内部弱下拉
#define ETH_MDC_PIN_1                 23                         // 8720 MDC引脚：GPIO23
#define ETH_MDIO_PIN_1                18                         // 8720 MDO引脚：GPIO18
// ESP32 PINS TO CONTROL CC2652P
#define CC2652P_RST                   16                         // CC2652P 复位引脚
#define CC2652P_FLASH                 32                         // CC2652P 片选引脚
#define CC2652P_RXD                   36                         // CC2652P 串口接收引脚
#define CC2652P_TXD                   4                          // CC2652P 串口发送引脚
#define BTN                           35                         // BTN 按键
#define MODE_SWITCH                   33                         // MODE SWITCH 按键


#define DEBOUNCE_TIME 70

#define TCP_LISTEN_PORT 9999
#define FORMAT_LITTLEFS_IF_FAILED true

// CC2652 settings (FOR BSL VALIDATION!)
#define NEED_BSL_PIN 15  // CC2652 pin number (FOR BSL VALIDATION!)
#define NEED_BSL_LEVEL 0 // 0-LOW 1-HIGH

const int16_t overseerInterval = 5 * 1000; // check lan or wifi connection every 5sec
const uint8_t overseerMaxRetry = 4;       // 5x4 = 20sec for PHY stability
const uint8_t LED_USB = 12;                // RED
const uint8_t LED_PWR = 14;                // BLUE
const uint8_t MAX_SOCKET_CLIENTS = 5;

enum COORDINATOR_MODE_t : uint8_t
{
  COORDINATOR_MODE_LAN,                // LAN模式
  COORDINATOR_MODE_WIFI,               // WIFI模式
  COORDINATOR_MODE_USB                 // USB模式
};

extern const char *coordMode;// coordMode node name
extern const char *prevCoordMode;// prevCoordMode node name
extern const char *configFileSystem;
extern const char *configFileWifi;
extern const char *configFileEther;
extern const char *configFileGeneral;
extern const char *configFileSecurity;
extern const char *configFileSerial;
extern const char *configFileMqtt;
extern const char *configFileWg;
extern const char *deviceModel;

struct ConfigSettingsStruct
{
    char ssid[50];                     // WiFi SSID
    char password[50];                 // WiFi password
    char ipAddressWiFi[18];            // WiFi IP address
    char ipMaskWiFi[16];               // WiFi mask
    char ipGWWiFi[18];                 // WiFi gateway
    bool dhcpWiFi;                     // WiFi DHCP
    bool dhcp;                         // Ethernet DHCP
    bool connectedEther;               // Ethernet connected
    char ipAddress[18];                // Ethernet IP address
    char ipMask[16];                   // Ethernet mask
    char ipGW[18];                     // Ethernet gateway
    int serialSpeed;                   // Serial speed
    int socketPort;                    // Socket port
    bool disableWeb;                   // Web server enabled
    int refreshLogs;                   // Logs refresh interval
    char hostname[50];                 // Hostname
    bool connectedSocket[10];          // Socket connected
    int connectedClients;              // Connected clients
    unsigned long socketTime;          // Socket time
    int tempOffset;                    // Temperature offset
    bool webAuth;                      // Web server authentication
    char webUser[50];                  // Web server user
    char webPass[50];                  // Web server password
    bool disableLedUSB;                // Disable LED USB
    bool disableLedPwr;                // Disable LED PWR
    // bool disablePingCtrl;
    bool disableLeds;                  // Disable LEDs
    COORDINATOR_MODE_t coordinator_mode;            // Coordinator mode
    COORDINATOR_MODE_t prevCoordinator_mode;        // Coordinator previous mode
    bool keepWeb;                      // Keep web server
    bool apStarted;                    // AP started
    bool wifiWebSetupInProgress;       // WiFi web setup in progress
    bool fwEnabled;                    // Firmware update enabled
    IPAddress fwIp;                    // Firmware update IP

    bool zbLedState;                   // ZB LED state
    bool zbFlashing;                   // ZB LED flashing
    char timeZone[50];                 // Time zone
};

struct MqttSettingsStruct
{
  bool enable;
  char server[50];
  IPAddress serverIP;
  int port;
  char user[50];
  char pass[50];
  char topic[50];
  // bool retain;
  int interval;
  bool discovery;
  unsigned long reconnectTime;
  unsigned long heartbeatTime;
};

struct WgSettingsStruct
{
  bool enable;
  bool init = 0;
  char localAddr[20];
  IPAddress localIP;
  char localPrivKey[45];
  char endAddr[45];
  char endPubKey[45];
  int endPort;
};

/*
struct InfosStruct
{
  char device[8];
  char mac[8];
  char flash[8];
};
*/

struct zbVerStruct
{
  uint32_t zbRev;
  uint8_t maintrel;
  uint8_t minorrel;
  uint8_t majorrel;
  uint8_t product;
  uint8_t transportrev;
  String chipID;
};

typedef CircularBuffer<char, 8024> LogConsoleType;

// #define WL_MAC_ADDR_LENGTH 6

#define DEBUG

#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(String(x))
#define DEBUG_PRINTLN(x) Serial.println(String(x))
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif
#endif
