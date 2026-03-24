# AVATTO GW90 Zigbee 网关固件 — 技术交接文档

> **项目名称**: avatto-gw90-firmware (PREGW90)  
> **当前版本**: v1.0.1  
> **固件构建时间**: 2024-03-26  
> **基础仓库**: mercenaruss/uzg-firmware (fork)  
> **文档编写日期**: 2026-02-24

---

## 目录

1. [项目概述](#1-项目概述)
2. [硬件平台说明](#2-硬件平台说明)
3. [开发环境与工具链](#3-开发环境与工具链)
4. [项目目录结构](#4-项目目录结构)
5. [核心源码模块说明](#5-核心源码模块说明)
6. [配置文件体系](#6-配置文件体系)
7. [Flash 分区表](#7-flash-分区表)
8. [三方库依赖](#8-三方库依赖)
9. [构建流程与自动化脚本](#9-构建流程与自动化脚本)
10. [Web 前端构建](#10-web-前端构建)
11. [固件功能特性](#11-固件功能特性)
12. [MQTT 集成](#12-mqtt-集成)
13. [WireGuard VPN 支持](#13-wireguard-vpn-支持)
14. [Zigbee 协调器管理](#14-zigbee-协调器管理)
15. [OTA 与固件升级](#15-ota-与固件升级)
16. [按键与 LED 交互逻辑](#16-按键与-led-交互逻辑)
17. [网络连接管理](#17-网络连接管理)
18. [调试方法](#18-调试方法)
19. [已知问题与注意事项](#19-已知问题与注意事项)
20. [后续开发建议](#20-后续开发建议)

---

## 1. 项目概述

本项目为 **AVATTO GW90-TI** Zigbee 网关的 ESP32 固件，基于开源项目 `mercenaruss/uzg-firmware` 定制开发。核心功能是将 **CC2652P Zigbee 协调器**通过 ESP32 桥接到网络（LAN/WiFi/USB），使其可被 Home Assistant（ZHA/Zigbee2MQTT）等智能家居平台使用。

**主要功能：**
- 三种协调器工作模式：LAN (以太网)、WiFi、USB 直连
- 内嵌 Web 管理界面（配置网络、串口、安全、MQTT 等）
- MQTT 集成与 Home Assistant 自动发现
- WireGuard VPN 隧道支持
- CC2652P Zigbee 芯片固件在线烧录（BSL 模式）
- mDNS 服务发现
- TCP Socket 透传（Zigbee 数据桥接）

---

## 2. 硬件平台说明

### 2.1 主控芯片

| 组件 | 型号 | 说明 |
|------|------|------|
| 主控 MCU | **ESP32-SOLO1** | 单核 Xtensa LX6, 160MHz |
| Zigbee 协调器 | **CC2652P** | TI, 支持 Zigbee 3.0 |
| 以太网 PHY | **LAN8720** | 通过 RMII 接口连接 |
| Flash | **16MB** | 外部 SPI Flash |

### 2.2 GPIO 引脚分配

| 引脚 | GPIO | 功能 | 说明 |
|------|------|------|------|
| CC2652P_RST | GPIO 16 | Zigbee 复位 | 输出，高电平有效 |
| CC2652P_FLASH | GPIO 32 | Zigbee BSL 模式 | 输出，用于进入烧录模式 |
| CC2652P_RXD | GPIO 36 | Zigbee 串口 RX | Serial2 接收 |
| CC2652P_TXD | GPIO 4 | Zigbee 串口 TX | Serial2 发送 |
| BTN | GPIO 35 | 用户按键 | 输入，下降沿中断 |
| MODE_SWITCH | GPIO 33 | USB/串口模式开关 | 输出，HIGH=USB模式 |
| LED_USB | GPIO 12 | 红色 LED | USB 模式指示 |
| LED_PWR | GPIO 14 | 蓝色 LED | 电源指示 |
| ETH_MDC | GPIO 23 | LAN8720 MDC | 以太网管理 |
| ETH_MDIO | GPIO 18 | LAN8720 MDIO | 以太网管理 |
| ETH_CLK | GPIO 17 | LAN8720 时钟输出 | `ETH_CLOCK_GPIO17_OUT` |
| ETH_PWR_ALT | GPIO 5 | LAN8720 电源控制 | 硬件复位 LAN PHY |

### 2.3 CC2652P BSL 引脚

- BSL 验证引脚: CC2652 Pin **15**
- BSL 触发电平: **LOW** (0)

---

## 3. 开发环境与工具链

### 3.1 IDE 与框架

- **IDE**: PlatformIO (VSCode 插件)
- **框架**: Arduino for ESP32
- **ESP32 平台**:
  - **生产环境 (Solo)**: `tasmota/platform-espressif32 @ 2023.07.00` — 经验证在 ESP32-SOLO1 上работает стабильно
  - **标准 ESP32 开发板**: `espressif32 @ 6.4.0`

> ⚠️ 平台版本选择非常关键！`platformio.ini` 中有详细的版本测试记录，请勿随意升级平台版本。

### 3.2 环境变量说明

| 环境名称 | 说明 |
|----------|------|
| `prod-solo` | **默认生产环境**，ESP32-SOLO1，160MHz，16MB Flash |
| `debug-solo` | Solo 调试版本，启用 `-DDEBUG` 宏 |
| `prod` | 标准 ESP32 开发板生产版本 |
| `debug` | 标准 ESP32 调试版本 |

### 3.3 编译参数

```
board_build.f_cpu = 160000000L      # CPU 频率 160MHz
board_upload.flash_size = 16MB       # Flash 大小
board_build.partitions = partitions.csv  # 自定义分区表
monitor_speed = 115200               # 串口监视波特率
upload_speed = 460800                # 上传速度
```

### 3.4 必要的开发工具

- **Node.js + npm**: 用于构建 Web 前端 (gulp 打包 gzip)
- **Python 3**: 用于构建脚本 (版本管理、固件合并)
- **PlatformIO Core**: 编译与上传

---

## 4. 项目目录结构

```
PREGW90/
├── bin/                          # 构建输出（固件二进制文件）
│   ├── AVATTO-GW90-Ti.bin        # OTA 升级用固件
│   ├── AVATTO-GW90-Ti_v1.0.1.full.bin  # 完整固件（含 bootloader + 分区表）
│   ├── bootloader_dio_40m.bin    # Bootloader
│   └── partitions.bin            # 分区表二进制
│
├── lib/                          # 本地库（非 PIO 注册库）
│   ├── CCTools/                  # CC2652P 控制库（BSL 烧录、复位等）
│   ├── IntelHex/                 # Intel HEX 格式解析库（固件烧录）
│   └── WireGuard-ESP32/          # WireGuard VPN 实现
│
├── src/                          # 主要源代码
│   ├── main.cpp                  # 主程序入口（setup/loop、配置加载、模式切换）
│   ├── config.h                  # 全局配置结构体、引脚定义、常量
│   ├── version.h                 # 自动生成的版本头文件
│   ├── web.cpp / web.h           # Web 服务器、API、页面路由
│   ├── mqtt.cpp / mqtt.h         # MQTT 客户端、HA 自动发现
│   ├── etc.cpp / etc.h           # 工具函数（LED、Zigbee控制、配置读写）
│   ├── zb.cpp / zb.h             # Zigbee 固件管理（版本检测、BSL 烧录）
│   ├── log.cpp / log.h           # 环形日志缓冲区
│   ├── zones.h                   # 时区数据表
│   ├── webh/                     # 自动生成 — Web 资源 gzip 压缩后的 C 头文件
│   └── websrc/                   # Web 前端源码（HTML/CSS/JS/图片）
│       ├── html/                 # HTML 页面模板
│       ├── css/                  # 样式表
│       ├── js/                   # JavaScript
│       ├── img/                  # 图标与图片
│       └── gzipped/              # 中间 gzip 产物
│
├── tools/                        # 构建工具与脚本
│   ├── build.py                  # 后处理脚本（合并固件、命名）
│   ├── version_increment_pre.py  # 前处理脚本（版本号自增）
│   ├── merge_bin_esp.py          # 固件合并工具
│   ├── version                   # 版本号文件（当前 1.0.1）
│   ├── esptool.py                # ESP 烧录工具
│   ├── flash.sh                  # 烧录脚本
│   └── webfilesbuilder/          # Web 前端构建工具
│       ├── build_html.py         # PIO 前处理：触发 npm/gulp 构建
│       ├── gulpfile.js           # Gulp 任务配置
│       ├── gulp.js / gulp.meta.js
│       └── package.json          # npm 依赖
│
├── debug-ui/                     # 调试用 Web UI 模拟器
├── platformio.ini                # PlatformIO 项目配置
├── partitions.csv                # Flash 分区表
├── manifest.json                 # ESP Web Tools 刷写清单
└── CHANGELOG.md                  # 变更日志
```

---

## 5. 核心源码模块说明

### 5.1 `main.cpp` (1506 行)

**程序入口**，包含：

| 函数 | 功能 |
|------|------|
| `setup()` | 初始化：GPIO、Serial2(Zigbee)、LittleFS、加载所有配置、LED 设置、协调器模式 |
| `loop()` | 主循环：按键处理、定时器更新、Web 服务、TCP Socket 透传、MQTT 循环 |
| `initLan()` | 初始化 LAN8720 以太网（含硬件复位 PHY） |
| `startServers()` | 启动 Web 服务、Socket 服务、mDNS、获取 Zigbee 版本 |
| `setupCoordinatorMode()` | 根据配置选择 LAN/WiFi/USB 模式并启动相应网络 |
| `handleTmrNetworkOverseer()` | 定时检查网络连接，超时后启动 AP 热点 |
| `NetworkEvent()` | WiFi/ETH 事件回调处理 |
| `connectWifi()` | WiFi STA 连接逻辑 |
| `startAP()` | 启动 AP 热点（fallback 或首次配置） |
| `toggleUsbMode()` | USB/非 USB 模式切换（保存配置并重启） |
| `loadConfig*()` | 系列配置加载函数（WiFi/Ether/General/Security/Serial/MQTT/WG） |
| `mDNS_start()` | mDNS 服务注册（含 ZHA zeroconf 信息） |

**TCP Socket 透传逻辑**（`loop()` 中）：
1. 最多支持 **5 个** TCP 客户端同时连接（`MAX_SOCKET_CLIENTS = 5`）
2. 从 TCP 客户端读取数据 → 写入 Serial2 (CC2652P)
3. 从 Serial2 读取数据 → 广播给所有已连接 TCP 客户端
4. 支持 IP 白名单过滤 (`fwEnabled` / `fwIp`)

### 5.2 `config.h` (160 行)

全局头文件，定义：

- **硬件引脚宏** (GPIO)
- **协调器模式枚举** `COORDINATOR_MODE_t`：`LAN(0)`, `WIFI(1)`, `USB(2)`
- **配置结构体**：
  - `ConfigSettingsStruct` — WiFi/以太网/串口/Web/LED/防火墙等
  - `MqttSettingsStruct` — MQTT 服务器/端口/认证/主题/间隔
  - `WgSettingsStruct` — WireGuard 隧道配置
  - `zbVerStruct` — Zigbee 固件版本信息
- **日志缓冲区类型**: `LogConsoleType` — `CircularBuffer<char, 8024>`
- **调试宏**: `DEBUG_PRINT` / `DEBUG_PRINTLN`

### 5.3 `web.cpp` (1944 行)

Web 管理界面，基于 **ESP32 WebServer**：

**页面路由**：
| URL | 功能 |
|-----|------|
| `/` | 仪表板首页 |
| `/general` | 通用设置（主机名、协调器模式、LED、时区） |
| `/wifi` | WiFi 配置 |
| `/ethernet` | 以太网配置 |
| `/serial` | 串口配置（波特率、端口号） |
| `/security` | 安全设置（Web 认证、防火墙） |
| `/mqtt` | MQTT 设置 |
| `/wg` | WireGuard VPN 设置 |
| `/about` | 设备信息 |
| `/sys-tools` | 系统工具（重启、固件升级等） |
| `/api` | JSON API 接口 |

**API 接口** (`/api`): 提供 JSON 格式的设备状态、配置数据，支持页面异步刷新。

**Web 资源处理**: 所有 HTML/JS/CSS/图片均预压缩为 gzip 字节数组，编译时嵌入固件，通过 `sendGzip()` 函数发送。

**安全特性**：
- 可选 HTTP Basic Auth (`checkAuth()`)
- IP 白名单防火墙

### 5.4 `mqtt.cpp` (467 行)

基于 **PubSubClient** 的 MQTT 客户端：

| 功能 | 说明 |
|------|------|
| `mqttConnectSetup()` | 初始化 MQTT 连接参数 |
| `mqttReconnect()` | 断线重连（带 LWT 遗嘱消息） |
| `mqttPublishState()` | 发布设备状态（uptime、温度、IP、模式、Zigbee 固件版本等） |
| `mqttPublishDiscovery()` | Home Assistant MQTT 自动发现（sensor/button/binary_sensor） |
| `mqttPublishIo()` | 发布 I/O 状态（重启/BSL/Socket） |
| `mqttCallback()` | 接收 MQTT 命令（`rst_esp`、`rst_zig`、`enbl_bsl`） |

**MQTT 主题结构**：
```
{topic}/avty        — 在线/离线 (LWT)
{topic}/state       — 设备状态 JSON
{topic}/io/{name}   — I/O 控制状态
{topic}/cmd         — 命令订阅
```

### 5.5 `etc.cpp` (373 行)

工具函数模块：

| 函数 | 功能 |
|------|------|
| `zigbeeRouterRejoin()` | Zigbee 路由器重新加入 |
| `zigbeeEnableBSL()` | 进入 CC2652P BSL 烧录模式 |
| `zigbeeRestart()` | 重启 CC2652P |
| `getCPUtemp()` | 读取 ESP32 CPU 温度（需临时开启 WiFi） |
| `getDeviceID()` | 生成设备唯一 ID |
| `writeDefaultConfig()` | 写入默认配置到 LittleFS |
| `resetSettings()` | 恢复出厂设置 |
| `setClock()` / `setTimezone()` | NTP 时间同步与时区设置 |
| `ledPowerToggle()` / `ledUSBToggle()` | LED 控制 |

**配置文件路径常量**（LittleFS 文件系统）：
```cpp
/config/system.json        — 系统变量（温度偏移）
/config/configWifi.json    — WiFi 配置
/config/configEther.json   — 以太网配置
/config/configGeneral.json — 通用设置
/config/configSecurity.json — 安全设置
/config/configSerial.json  — 串口配置
/config/configMqtt.json    — MQTT 配置
/config/configWg.json      — WireGuard 配置
```

### 5.6 `zb.cpp` (347 行)

Zigbee 协调器管理：

| 函数 | 功能 |
|------|------|
| `getZbVer()` | 通过 SYS_VERSION 命令获取 CC2652P 固件版本 |
| `zbCheck()` | 检测 Zigbee 模块是否正常（LED 闪烁测试） |
| `zbInit()` | 初始化 Zigbee 通讯 |
| `checkFwHex()` | 验证上传的 HEX 固件文件 |
| `runFlash()` / `programFlashFromFile()` | 通过 BSL 烧录 CC2652P 固件 |
| `zbLedToggle()` | 切换 Zigbee 模块 LED |

**Zigbee 通讯协议**: 使用 TI Z-Stack ZNP (Zigbee Network Processor) 协议，通过 Serial2 (UART) 与 CC2652P 通信。

### 5.7 `log.cpp` / `log.h`

环形日志缓冲区（8024 字节），用于 Web 控制台实时日志显示。

---

## 6. 配置文件体系

所有配置存储于 **LittleFS** 文件系统的 `/config/` 目录下，JSON 格式。

### 6.1 各配置文件字段说明

**`/config/configGeneral.json`**
```json
{
    "hostname": "AVATTO-GW90-TI",
    "disableLeds": false,
    "refreshLogs": 1000,
    "disableLedPwr": false,
    "disableLedUSB": false,
    "coordMode": 0,        // 0=LAN, 1=WiFi, 2=USB
    "prevCoordMode": 0,
    "keepWeb": false,       // USB 模式下保持 Web 可用
    "timeZoneName": ""
}
```

**`/config/configWifi.json`**
```json
{
    "enableWiFi": 0,
    "ssid": "",
    "pass": "",
    "dhcpWiFi": 1,
    "ip": "", "mask": "", "gw": ""
}
```

**`/config/configEther.json`**
```json
{
    "dhcp": 1,
    "ip": "", "mask": "", "gw": ""
}
```

**`/config/configSerial.json`**
```json
{
    "baud": 115200,
    "port": 6638
}
```

**`/config/configSecurity.json`**
```json
{
    "disableWeb": 0,
    "webAuth": 0,
    "webUser": "admin",
    "webPass": "",
    "fwEnabled": 0,
    "fwIp": ""
}
```

**`/config/configMqtt.json`**
```json
{
    "enable": 0,
    "server": "",
    "port": 1883,
    "user": "mqttuser",
    "pass": "",
    "topic": "{deviceId}",
    "interval": 60,
    "discovery": 0
}
```

**`/config/configWg.json`**
```json
{
    "enable": 0,
    "localAddr": "",
    "localIP": "",
    "endAddr": "",
    "endPubKey": "",
    "endPort": ""
}
```

### 6.2 配置加载流程 (setup)

```
loadSystemVar()        → 系统变量（温度偏移校准）
loadConfigSerial()     → 串口参数
loadConfigWifi()       → WiFi
loadConfigEther()      → 以太网
loadConfigGeneral()    → 通用设置
loadConfigSecurity()   → 安全
loadConfigMqtt()       → MQTT
loadConfigWg()         → WireGuard
```

任何配置加载失败会尝试创建默认配置；反复失败则 `ESP.restart()`。

---

## 7. Flash 分区表

**文件**: `partitions.csv`

| 分区名 | 类型 | 子类型 | 偏移地址 | 大小 | 说明 |
|--------|------|--------|----------|------|------|
| nvs | data | nvs | 0x9000 | 20KB | Non-Volatile Storage |
| otadata | data | ota | 0xE000 | 8KB | OTA 数据 |
| app0 | app | ota_0 | 0x10000 | **6MB** | 主应用分区 |
| app1 | app | ota_1 | 0x610000 | **6MB** | OTA 备用分区 |
| spiffs | data | spiffs | 0xC10000 | ~4MB | LittleFS 文件系统 |

> 总计使用 ~16MB Flash。支持 OTA 双分区切换升级。

---

## 8. 三方库依赖

### 8.1 PlatformIO lib_deps

| 库 | 版本 | 用途 |
|----|------|------|
| `bblanchon/ArduinoJson` | 6.21.3 | JSON 序列化/反序列化 |
| `rlogiacco/CircularBuffer` | ≥1.3.3 | 环形日志缓冲区 |
| `plerup/EspSoftwareSerial` | 8.1.0 | 软件串口（备用） |
| `marian-craciunescu/ESP32Ping` | ≥1.7 | Ping 网络检测 |
| `sstaub/Ticker` | ≥4.4.0 | 非阻塞定时器 |
| `knolleary/PubSubClient` | ^2.8 | MQTT 客户端 |
| `mathieucarbou/AsyncTCP` | 3.2.5 | 异步 TCP（依赖项） |
| `Martin-Laclaustra/CronAlarms` | latest | Cron 定时任务 |

### 8.2 本地库 (lib/)

| 库 | 用途 |
|----|------|
| **CCTools** | CC2652P 芯片控制（BSL 进入、复位、路由器操作） |
| **IntelHex** | Intel HEX 固件文件解析（Zigbee 固件烧录） |
| **WireGuard-ESP32** | ESP32 平台的 WireGuard VPN 实现 |

---

## 9. 构建流程与自动化脚本

### 9.1 编译命令

```bash
# 默认生产构建（ESP32-SOLO1）
pio run -e prod-solo

# 调试构建
pio run -e debug-solo

# 上传固件
pio run -e prod-solo -t upload

# 监视串口
pio device monitor
```

### 9.2 自动构建流程

PlatformIO 通过 `extra_scripts` 配置自动执行以下流程：

#### 前处理（Pre-build）

1. **`tools/version_increment_pre.py`**
   - 读取 `tools/version` 文件中的版本号
   - 自动递增补丁版本号（除非存在 `.version_no_increment` 标记文件）
   - 生成 `src/version.h`（含 `VERSION` 和 `BUILD_TIMESTAMP` 宏）

2. **`tools/webfilesbuilder/build_html.py`**
   - 检查 `.no_web_update` 标记文件
   - 执行 `npm install` + `npx gulp` 构建 Web 前端
   - 将 HTML/JS/CSS/图片压缩为 gzip 并转为 C 头文件，输出到 `src/webh/`

#### 后处理（Post-build）

3. **`tools/build.py`**
   - 复制 `firmware.bin` 到 `bin/`
   - 复制 `partitions.bin` 和 `bootloader_dio_40m.bin`
   - 调用 `merge_bin_esp.py` 合并完整固件：
     - `bootloader @ 0x1000` + `partitions @ 0x8000` + `firmware @ 0x10000`
   - 生成命名文件：
     - `AVATTO-GW90-Ti_v{version}.full.bin` — 全量烧录
     - `AVATTO-GW90-Ti.bin` — OTA 升级

### 9.3 版本管理

- 版本号存储在 `tools/version` 文本文件中
- 格式: `主版本.次版本.补丁号` (如 `1.0.1`)
- 每次构建自动 +1（补丁号），除非：
  - 存在 `tools/.version_no_increment` 文件（完全跳过）
  - 存在 `tools/.version_no_increment_update_date` 文件（不增版本但更新时间戳）

### 9.4 跳过 Web 构建

如只修改了 C++ 代码，可在 `tools/webfilesbuilder/` 下创建 `.no_web_update` 空文件跳过 Web 构建，加快编译速度。

---

## 10. Web 前端构建

### 10.1 技术栈

- **HTML 模板**: 位于 `src/websrc/html/`
- **JavaScript**: jQuery + Bootstrap + 自定义 `functions.js`
- **国际化**: i18next
- **构建工具**: Gulp (通过 `tools/webfilesbuilder/gulpfile.js`)

### 10.2 构建流程

```
src/websrc/ (源码)
    ↓ gulp (压缩 + gzip)
src/websrc/gzipped/ (中间产物)
    ↓ 转换为 C 字节数组
src/webh/*.gz.h (编译时嵌入固件)
```

### 10.3 手动构建 Web 资源

```bash
cd tools/webfilesbuilder
npm install
npx gulp
```

---

## 11. 固件功能特性

### 11.1 三种工作模式

| 模式 | `coordMode` | 说明 |
|------|-------------|------|
| **LAN** | 0 | Zigbee 数据通过以太网 TCP Socket 透传，默认端口 6638 |
| **WiFi** | 1 | Zigbee 数据通过 WiFi TCP Socket 透传 |
| **USB** | 2 | GPIO33 拉高，CC2652P 直接通过 USB 与主机通讯 |

### 11.2 网络服务

| 服务 | 端口 | 说明 |
|------|------|------|
| Web 管理界面 | TCP 80 | HTTP |
| TCP Socket 透传 | TCP 6638 (默认, 可配置) | Zigbee 数据桥 |
| mDNS | UDP 5353 | 服务发现 (`_avatto-gw90-ti._tcp`) |
| DNS (AP 模式) | UDP 53 | 强制门户 |

### 11.3 AP 热点模式

- **SSID**: 设备 ID (如 `AVATTO-GW90-TI`)
- **IP**: 192.168.1.1
- **触发条件**:
  - 首次启动无配置
  - 网络连接超时（5秒 × 4次 = 20秒）
  - WiFi 无凭据

---

## 12. MQTT 集成

### 12.1 功能

- 定时发布设备状态（可配置间隔，默认 60 秒）
- Home Assistant MQTT 自动发现（sensor/button/binary_sensor）
- 远程命令控制
- LWT 遗嘱消息（在线/离线检测）

### 12.2 远程命令

通过 `{topic}/cmd` 主题发送 JSON：
```json
{"cmd": "rst_esp"}     // 重启 ESP32
{"cmd": "rst_zig"}     // 重启 Zigbee
{"cmd": "enbl_bsl"}    // 进入 BSL 模式
```

### 12.3 状态数据

`{topic}/state` 发布的 JSON：
```json
{
    "uptime": "0 d 01:23:45",
    "temperature": "42.5",
    "connections": 1,
    "ip": "192.168.1.100",
    "mode": "Zigbee-to-Ethernet",
    "zbfw": "20221226",
    "hostname": "AVATTO-GW90-TI"
}
```

---

## 13. WireGuard VPN 支持

- 基于本地库 `WireGuard-ESP32`
- 当网络连接建立后自动初始化 WireGuard 隧道
- 配置项：本地地址/私钥、端点地址/公钥/端口

---

## 14. Zigbee 协调器管理

### 14.1 CC2652P 固件版本检测

通过 ZNP 协议的 `SYS_VERSION` 命令获取版本信息，包括：
- `majorrel` / `minorrel` / `maintrel` / `zbRev` / `product` / `transportrev`

### 14.2 BSL 固件烧录

1. 通过 Web 界面上传 `.hex` 固件文件
2. 文件保存到 LittleFS `/config/fw.hex`
3. 使用 CCTools 库将 CC2652P 置入 BSL 模式
4. 通过串口执行烧录

### 14.3 LED 控制

通过 ZNP 命令控制 CC2652P 模块上的 LED：
- 命令帧格式: `FE 02 27 0A 01 {00|01} {checksum}`

---

## 15. OTA 与固件升级

### 15.1 Web OTA

通过 Web 界面 `/sys-tools` 上传 `.bin` 文件进行 OTA 升级。

### 15.2 远程 OTA

固件从 GitHub Release 下载：
```
https://github.com/AVATTO-smart/avatto-gw90-firmware/releases/latest/download/AVATTO-GW90-Ti.bin
```

### 15.3 ESP Web Tools 烧录

`manifest.json` 支持通过浏览器 Web Serial 直接烧录完整固件（含 bootloader + 分区表 + 应用）。

---

## 16. 按键与 LED 交互逻辑

### 16.1 物理按键 (GPIO 35)

| 操作 | 效果 |
|------|------|
| **短按** | 切换 USB/非USB 模式（保存并重启） |
| **长按 2 秒** | 切换 LED 全局开关 |
| **长按 4 秒** | 进入 Zigbee BSL 烧录模式 |
| **开机时按住 >2 秒** | 恢复出厂设置 |

### 16.2 LED 指示

| LED | 颜色 | GPIO | 指示 |
|-----|------|------|------|
| LED_PWR | 蓝色 | 14 | 电源/运行指示（可禁用） |
| LED_USB | 红色 | 12 | USB 模式指示（仅 USB 模式亮） |

---

## 17. 网络连接管理

### 17.1 网络监控定时器 (Network Overseer)

- **检查间隔**: 5 秒
- **最大重试次数**: 4 次 (即 20 秒超时)
- **行为**:
  - LAN 模式: 检测以太网连接，超时启动 AP
  - WiFi 模式: 检测 WiFi 连接，超时重新连接并启动 AP
  - USB 模式: 10 秒尝试 WiFi，失败后尝试 LAN，均失败启动 AP

### 17.2 以太网初始化

LAN8720 PHY 初始化流程：
1. GPIO5 拉低 50ms → 拉高 300ms（硬件复位 PHY）
2. `ETH.begin()` 初始化 RMII 接口
3. 配置静态 IP 或使用 DHCP

### 17.3 mDNS 服务

注册 `_avatto-gw90-ti._tcp` 服务，包含 ZHA 兼容的 zeroconf 元数据：
- `radio_type`: znp
- `baud_rate`: 配置的波特率
- `data_flow_control`: software

---

## 18. 调试方法

### 18.1 编译调试版本

```bash
pio run -e debug-solo
```
启用 `-DDEBUG` 宏后，`DEBUG_PRINT` / `DEBUG_PRINTLN` 宏将输出到 Serial（115200 baud）。

### 18.2 串口监视器

```bash
pio device monitor
```
内置 `esp32_exception_decoder` 过滤器，可自动解码异常堆栈。

### 18.3 Web 日志

Web 界面提供实时日志查看（环形缓冲区 8024 字节），可查看 Zigbee 通讯数据（HEX 格式）。

### 18.4 Debug UI

`debug-ui/` 目录提供独立的调试 Web 界面，可通过 `start-debug-server.bat` 启动，用 mock 数据模拟设备行为。

### 18.5 关键调试日志前缀

| 前缀 | 模块 |
|------|------|
| `[LAN]` | 以太网初始化 |
| `[ETH_EVENT]` | 以太网事件（连接/断开/获取IP） |
| `[NET_EVENT]` | 网络事件分发 |
| `[OVERSEER]` | 网络监控定时器 |
| `[SRV]` | 服务启动 |
| `[MODE]` | 协调器模式切换 |
| `[LED]` | LED 控制状态 |
| `[ZBVER]` | Zigbee 版本信息 |
| `[ZB_FW_TASK]` | Zigbee 固件烧录任务 |
| `[SOCK IP WHITELIST]` | Socket 连接 IP 过滤 |

---

## 19. 已知问题与注意事项

### 19.1 平台版本兼容性

**非常重要**: ESP32-SOLO1 的平台版本选择至关重要。经测试：
- ✅ `tasmota/platform-espressif32 @ 2023.07.00` — 稳定工作
- ❌ `2023.08.00` 及之后版本 — HTTPS 功能失败
- ❌ `espressif32 @ 6.4.0` — 不支持 SOLO1 板型

### 19.2 LAN8720 PHY 初始化

`ETH.begin()` 前必须对 GPIO5 执行硬件复位（LOW 50ms → HIGH 300ms），否则 `ESP.restart()` 后 PHY 可能无法正常初始化。

### 19.3 CPU 温度读取

ESP32 温度传感器需要 WiFi 模块处于激活状态才能工作。`getCPUtemp()` 函数会在读取时临时开启再关闭 WiFi。

### 19.4 MQTT 缓冲区

PubSubClient 默认缓冲区较小，`mqttPublishMsg()` 使用了 `beginPublish/print/endPublish` 模式以支持大消息。

### 19.5 配置文件损坏处理

所有 `loadConfig*()` 函数在 JSON 反序列化失败时会**删除损坏文件**并重启设备，下次启动时会自动创建默认配置。

### 19.6 版本号存储

`tools/version` 文件仅保存版本号字符串，不在 Git 提交中自动同步。手动修改版本号需直接编辑此文件。


## 附录 A: 快速上手指南

### 首次编译

```bash
# 1. 安装 PlatformIO CLI (或 VSCode 插件)
pip install platformio

# 2. 安装 Node.js (用于 Web 构建)
# 下载: https://nodejs.org/

# 3. 克隆项目
git clone <repo_url>
cd PREGW90

# 4. 编译
pio run -e prod-solo

# 5. 输出文件
# bin/AVATTO-GW90-Ti.bin          — OTA 升级固件
# bin/AVATTO-GW90-Ti_v*.full.bin  — 全量烧录固件
```

### 首次烧录

```bash
# 通过串口烧录
pio run -e prod-solo -t upload

# 或使用 esptool 手动烧录完整固件
python tools/esptool.py --chip esp32 --baud 460800 write_flash 0x0 bin/AVATTO-GW90-Ti_v1.0.1.full.bin
```

### 设备首次配置

1. 设备首次启动会创建 AP 热点 (SSID: `AVATTO-GW90-TI`)
2. 连接热点后浏览器访问 `192.168.1.1`
3. 配置网络参数（WiFi 或以太网）
4. 设备重启后通过配置的网络访问 Web 管理界面

---

## 附录 B: 文件行数统计

| 文件 | 行数 | 说明 |
|------|------|------|
| `main.cpp` | 1506 | 程序入口与核心逻辑 |
| `web.cpp` | 1944 | Web 服务器 (最大文件) |
| `mqtt.cpp` | 467 | MQTT 集成 |
| `etc.cpp` | 373 | 工具函数 |
| `zb.cpp` | 347 | Zigbee 管理 |
| `config.h` | 160 | 配置定义 |
| `zones.h` | 479 | 时区数据 |

---

## 附录 C: 联系与资源

- **原始仓库**: https://github.com/mercenaruss/uzg-firmware
- **OTA 固件下载**: https://github.com/AVATTO-smart/avatto-gw90-firmware/releases
- **PlatformIO 文档**: https://docs.platformio.org/
- **CC2652P BSL 文档**: TI CC26xx Technical Reference Manual
- **Z-Stack ZNP 接口**: TI Z-Stack ZNP Interface Specification

---


