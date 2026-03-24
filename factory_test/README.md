# AVATTO GW90 产测程序方案

## 概述

本产测程序用于 AVATTO GW90-TI Zigbee 网关的产线全功能测试。  
测试覆盖 ESP32-SOLO1 主控、CC2652P Zigbee 协调器、LAN8720 以太网 PHY、WiFi、LED、按键等所有硬件。

## 测试架构

```
┌────────────────────┐      串口(115200)      ┌─────────────────┐
│   产测上位机 (PC)   │◄═══════════════════════►│  AVATTO GW90    │
│   (Python 脚本)    │      USB-TTL           │  (产测固件)      │
└────────────────────┘                         └─────────────────┘
        │                                             │
        │  TCP (可选)                                  │  RJ45
        └──────────────────────────────────────────────┘
```

## 测试项清单

| # | 测试项 | 方法 | 判定标准 | 耗时 |
|---|--------|------|----------|------|
| 1 | ESP32 基本启动 | 串口输出检测 | 能正常输出启动信息 | 1s |
| 2 | Flash 读写 (16MB) | LittleFS 写读校验 | 读写一致 | 2s |
| 3 | ESP32 MAC 地址 | 读取 eFuse MAC | MAC 非全0/全F | <1s |
| 4 | CPU 温度传感器 | 读取内部温度 | 0°C ~ 80°C 范围内 | 1s |
| 5 | WiFi 扫描 | WiFi.scanNetworks() | 至少扫到 1 个 AP | 5s |
| 6 | 以太网 PHY (LAN8720) | ETH.begin() + linkUp | 获得 link up 或 DHCP IP | 8s |
| 7 | CC2652P 通讯 | BSL 握手 / SYS_VERSION | 正常返回版本信息 | 3s |
| 8 | CC2652P 芯片 ID | CCTools.detectChipInfo() | 识别为 CC2652P/P7 | 2s |
| 9 | 蓝色 LED (PWR) | GPIO14 输出 + 人工确认/光敏 | LED 亮灭可控 | 2s |
| 10 | 红色 LED (USB) | GPIO12 输出 + 人工确认/光敏 | LED 亮灭可控 | 2s |
| 11 | 按键 (BTN) | GPIO35 输入检测 | 按下=LOW, 松开=HIGH | 3s |
| 12 | MODE_SWITCH | GPIO33 输出 + 回读(如可行) | 高低电平可切换 | 1s |
| 13 | Zigbee LED 控制 | ZNP 命令控制 CC2652P LED | 正确响应 | 2s |
| 14 | 串口透传 | Serial2 收发环回 | 数据一致 | 1s |
| 15 | 写入产测标记 | NVS/LittleFS 写标记 | 标记写入成功 | <1s |

**预计单板总测试时间**: 约 30-35 秒

## 构建方法

```bash
# 在 PlatformIO 中编译产测固件
pio run -e factory-test-solo
```

## 上位机使用

```bash
cd factory_test
pip install pyserial
python host_test.py --port COM3
```
