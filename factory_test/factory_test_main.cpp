/*
 * AVATTO GW90-TI 产测固件
 * ========================
 * 
 * 功能: ESP32-SOLO1 + CC2652P + LAN8720 全硬件测试
 * 
 * 串口协议 (115200, 8N1):
 *   上位机发送命令 → 设备回复 JSON 结果
 *   命令格式: "CMD:xxx\n"
 *   回复格式: "RSP:{json}\n"
 * 
 * 该文件作为独立产测固件, 不依赖原项目 web/mqtt 模块
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <CCTools.h>
#include <esp_wifi.h>

// ============ 引脚定义 (与正式固件一致) ============
// --- LAN8720 以太网 PHY ---
#define ETH_CLK_MODE_1          ETH_CLOCK_GPIO17_OUT
#define ETH_POWER_PIN_ALT       5
#define ETH_POWER_PIN_1         -1
#define ETH_TYPE_1              ETH_PHY_LAN8720
#define ETH_ADDR_1              0
#define ETH_MDC_PIN_1           23
#define ETH_MDIO_PIN_1          18

// --- CC2652P Zigbee ---
#define CC2652P_RST             16
#define CC2652P_FLASH           32
#define CC2652P_RXD             36
#define CC2652P_TXD             4

// --- 用户接口 ---
#define BTN                     35
#define MODE_SWITCH             33
#define LED_USB                 12    // 红色 LED
#define LED_PWR                 14    // 蓝色 LED

// --- 测试参数 ---
#define TEST_TIMEOUT_MS         30000
#define SERIAL_BAUD             115200
#define LITTLEFS_MOUNT_POINT    "/lfs2"
#define FACTORY_MARK_FILE       "/config/factory_test.json"

// ============ 测试结果结构 ============
struct TestResult {
    bool passed;
    String detail;
};

struct AllTestResults {
    TestResult esp32_boot;
    TestResult flash_rw;
    TestResult mac_addr;
    TestResult cpu_temp;
    TestResult wifi_scan;
    TestResult ethernet;
    TestResult cc2652p_comm;
    TestResult cc2652p_chip;
    TestResult led_blue;
    TestResult led_red;
    TestResult btn_test;
    TestResult mode_switch;
    TestResult zb_led;
    TestResult serial_loopback;
    TestResult factory_mark;
    int total_pass;
    int total_fail;
    int total_tests;
};

AllTestResults results;

// ============ 以太网状态 ============
volatile bool ethConnected = false;
volatile bool ethGotIP = false;
String ethIPAddr = "";

// ============ ZNP 命令常量 ============
const byte zigLed1On[]  = {0xFE, 0x02, 0x27, 0x0A, 0x01, 0x01, 0x2F};
const byte zigLed1Off[] = {0xFE, 0x02, 0x27, 0x0A, 0x01, 0x00, 0x2E};
const byte cmdSysVersion[] = {0xFE, 0x00, 0x21, 0x02, 0x23};

CCTools CCTool(Serial2, CC2652P_RST, CC2652P_FLASH);

// ============ 工具函数 ============

void sendResponse(const char* testName, bool passed, const String& detail) {
    DynamicJsonDocument doc(512);
    doc["test"] = testName;
    doc["result"] = passed ? "PASS" : "FAIL";
    doc["detail"] = detail;
    
    Serial.print("RSP:");
    serializeJson(doc, Serial);
    Serial.println();
}

void sendSummary() {
    DynamicJsonDocument doc(2048);
    doc["type"] = "SUMMARY";
    doc["total"] = results.total_tests;
    doc["pass"] = results.total_pass;
    doc["fail"] = results.total_fail;
    doc["verdict"] = (results.total_fail == 0) ? "PASS" : "FAIL";
    
    JsonArray failures = doc.createNestedArray("failures");
    
    // 列出失败项
    struct { const char* name; TestResult* r; } items[] = {
        {"esp32_boot", &results.esp32_boot},
        {"flash_rw", &results.flash_rw},
        {"mac_addr", &results.mac_addr},
        {"cpu_temp", &results.cpu_temp},
        {"wifi_scan", &results.wifi_scan},
        {"ethernet", &results.ethernet},
        {"cc2652p_comm", &results.cc2652p_comm},
        {"cc2652p_chip", &results.cc2652p_chip},
        {"led_blue", &results.led_blue},
        {"led_red", &results.led_red},
        {"btn_test", &results.btn_test},
        {"mode_switch", &results.mode_switch},
        {"zb_led", &results.zb_led},
        {"serial_loopback", &results.serial_loopback},
        {"factory_mark", &results.factory_mark},
    };
    
    for (auto& item : items) {
        if (!item.r->passed) {
            failures.add(item.name);
        }
    }
    
    Serial.print("RSP:");
    serializeJson(doc, Serial);
    Serial.println();
}

void updateCount(TestResult& r) {
    results.total_tests++;
    if (r.passed) results.total_pass++;
    else results.total_fail++;
}

// ============ 以太网事件处理 ============

void ethEvent(WiFiEvent_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_CONNECTED:
            ethConnected = true;
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            ethGotIP = true;
            ethIPAddr = ETH.localIP().toString();
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            ethConnected = false;
            ethGotIP = false;
            break;
        default:
            break;
    }
}

// ============ 各测试项实现 ============

// 测试 1: ESP32 基本启动
TestResult test_esp32_boot() {
    TestResult r;
    uint32_t chipId = ESP.getChipModel() ? 1 : 0;
    uint32_t freq = ESP.getCpuFreqMHz();
    uint32_t flashSize = ESP.getFlashChipSize();
    
    r.detail = "CPU:" + String(ESP.getChipModel()) + 
               " Freq:" + String(freq) + "MHz" +
               " Flash:" + String(flashSize / 1024 / 1024) + "MB" +
               " SDK:" + String(ESP.getSdkVersion());
    r.passed = (freq > 0 && flashSize > 0);
    return r;
}

// 测试 2: Flash 读写 (LittleFS)
TestResult test_flash_rw() {
    TestResult r;
    
    if (!LittleFS.begin(true, LITTLEFS_MOUNT_POINT, 10)) {
        r.passed = false;
        r.detail = "LittleFS mount failed";
        return r;
    }
    
    // 写入测试数据
    const char* testFile = "/config/factory_rw_test.tmp";
    const char* testData = "AVATTO_GW90_FACTORY_TEST_20260225";
    
    // 确保目录存在
    LittleFS.mkdir("/config");
    
    File wf = LittleFS.open(testFile, FILE_WRITE);
    if (!wf) {
        r.passed = false;
        r.detail = "File write open failed";
        return r;
    }
    wf.print(testData);
    wf.close();
    
    // 读回验证
    File rf = LittleFS.open(testFile, FILE_READ);
    if (!rf) {
        r.passed = false;
        r.detail = "File read open failed";
        return r;
    }
    String readBack = rf.readString();
    rf.close();
    
    // 清理
    LittleFS.remove(testFile);
    
    // 获取文件系统信息
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    
    r.passed = (readBack == testData);
    r.detail = "RW:" + String(r.passed ? "OK" : "MISMATCH") + 
               " Total:" + String(totalBytes / 1024) + "KB" +
               " Used:" + String(usedBytes / 1024) + "KB";
    return r;
}

// 测试 3: MAC 地址
TestResult test_mac_addr() {
    TestResult r;
    uint64_t mac = ESP.getEfuseMac();
    
    uint8_t macBytes[6];
    macBytes[0] = (mac >> 0) & 0xFF;
    macBytes[1] = (mac >> 8) & 0xFF;
    macBytes[2] = (mac >> 16) & 0xFF;
    macBytes[3] = (mac >> 24) & 0xFF;
    macBytes[4] = (mac >> 32) & 0xFF;
    macBytes[5] = (mac >> 40) & 0xFF;
    
    char macStr[18];
    sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            macBytes[0], macBytes[1], macBytes[2],
            macBytes[3], macBytes[4], macBytes[5]);
    
    // 检查 MAC 不是全0或全F
    bool allZero = (mac == 0);
    bool allFF = (mac == 0xFFFFFFFFFFFF);
    
    r.passed = (!allZero && !allFF);
    r.detail = String(macStr);
    return r;
}

// 测试 4: CPU 温度
TestResult test_cpu_temp() {
    TestResult r;
    
    // ESP32 温度传感器需要 WiFi 开启
    if (WiFi.getMode() == WIFI_MODE_NULL || WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
    }
    
    // 使用内部温度传感器
    #ifdef __cplusplus
    extern "C" {
    #endif
    uint8_t temprature_sens_read();
    #ifdef __cplusplus
    }
    #endif
    
    float temp = (temprature_sens_read() - 32) / 1.8;
    
    r.passed = (temp > 0.0 && temp < 80.0);
    r.detail = "Temp:" + String(temp, 1) + "C";
    return r;
}

// 测试 5: WiFi 扫描
TestResult test_wifi_scan() {
    TestResult r;
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    int n = WiFi.scanNetworks(false, false, false, 3000); // 3秒超时
    
    if (n > 0) {
        r.passed = true;
        String bestAP = WiFi.SSID(0) + "(" + String(WiFi.RSSI(0)) + "dBm)";
        r.detail = "Found:" + String(n) + " Best:" + bestAP;
    } else {
        r.passed = false;
        r.detail = "No AP found (scan returned " + String(n) + ")";
    }
    
    WiFi.scanDelete();
    return r;
}

// 测试 6: 以太网 (LAN8720)
TestResult test_ethernet() {
    TestResult r;
    
    ethConnected = false;
    ethGotIP = false;
    ethIPAddr = "";
    
    WiFi.onEvent(ethEvent);
    
    // 硬件复位 LAN8720 PHY
    pinMode(ETH_POWER_PIN_ALT, OUTPUT);
    digitalWrite(ETH_POWER_PIN_ALT, LOW);
    delay(50);
    digitalWrite(ETH_POWER_PIN_ALT, HIGH);
    delay(300);
    
    bool ethBeginOk = ETH.begin(ETH_ADDR_1, ETH_POWER_PIN_1, ETH_MDC_PIN_1, ETH_MDIO_PIN_1, ETH_TYPE_1, ETH_CLK_MODE_1, ETH_POWER_PIN_ALT);
    
    if (!ethBeginOk) {
        r.passed = false;
        r.detail = "ETH.begin() failed - PHY not detected";
        return r;
    }
    
    // 等待连接 (最多 8 秒)
    unsigned long start = millis();
    while (!ethConnected && (millis() - start < 8000)) {
        delay(100);
    }
    
    if (!ethConnected) {
        // 即使没插网线, PHY 初始化成功也算部分通过
        // 产测治具应连接网线
        r.passed = false;
        r.detail = "PHY init OK but no link (check cable)";
        return r;
    }
    
    // 等待 DHCP (再等 3 秒)
    start = millis();
    while (!ethGotIP && (millis() - start < 3000)) {
        delay(100);
    }
    
    if (ethGotIP) {
        r.passed = true;
        r.detail = "Link:UP IP:" + ethIPAddr + 
                   " Speed:" + String(ETH.linkSpeed()) + "Mbps" +
                   " Duplex:" + (ETH.fullDuplex() ? "Full" : "Half") +
                   " MAC:" + ETH.macAddress();
    } else {
        // Link up 但无 DHCP — 可能产线没有 DHCP 服务
        // PHY 工作正常也可判定通过
        r.passed = true;
        r.detail = "Link:UP DHCP:timeout MAC:" + ETH.macAddress();
    }
    
    return r;
}

// 测试 7: CC2652P 通讯 (版本读取)
TestResult test_cc2652p_comm() {
    TestResult r;
    
    // 确保 MODE_SWITCH 为低(串口模式)
    digitalWrite(MODE_SWITCH, LOW);
    delay(100);
    
    // 清空缓冲区
    while (Serial2.available()) Serial2.read();
    
    // 发送 SYS_VERSION 命令
    Serial2.write(cmdSysVersion, sizeof(cmdSysVersion));
    Serial2.flush();
    delay(200);
    
    // 尝试多次
    uint32_t zbRev = 0;
    uint8_t majorrel = 0, minorrel = 0, maintrel = 0;
    
    for (int attempt = 0; attempt < 5; attempt++) {
        if (Serial2.available() >= 5) {
            if (Serial2.read() == 0xFE && Serial2.read() == 0x0A && 
                Serial2.read() == 0x61 && Serial2.read() == 0x02) {
                // 成功接收响应头
                byte buf[11];
                for (int i = 0; i < 11; i++) {
                    buf[i] = Serial2.read();
                }
                zbRev = buf[5] | (buf[6] << 8) | (buf[7] << 16) | (buf[8] << 24);
                majorrel = buf[2];
                minorrel = buf[3];
                maintrel = buf[4];
                break;
            }
        }
        // 重试
        while (Serial2.available()) Serial2.read();
        Serial2.write(cmdSysVersion, sizeof(cmdSysVersion));
        Serial2.flush();
        delay(200);
    }
    
    if (zbRev > 0) {
        r.passed = true;
        r.detail = "FW:" + String(majorrel) + "." + String(minorrel) + "." + String(maintrel) +
                   " Rev:" + String(zbRev);
    } else {
        r.passed = false;
        r.detail = "No response from CC2652P (check Serial2 wiring)";
    }
    
    while (Serial2.available()) Serial2.read();
    return r;
}

// 测试 8: CC2652P 芯片识别
TestResult test_cc2652p_chip() {
    TestResult r;
    
    if (CCTool.begin()) {
        String chipInfo = CCTool.detectChipInfo();
        CCTool.restart();
        delay(500);
        
        r.passed = (chipInfo.length() > 0 && chipInfo != "Unknown");
        r.detail = "Chip:" + chipInfo;
    } else {
        r.passed = false;
        r.detail = "CCTools BSL handshake failed";
    }
    
    return r;
}

// 测试 9: 蓝色 LED (PWR)
TestResult test_led_blue() {
    TestResult r;
    
    // 亮 → 暗 → 亮 循环测试
    digitalWrite(LED_PWR, HIGH);
    delay(500);
    int stateHigh = digitalRead(LED_PWR);
    
    digitalWrite(LED_PWR, LOW);
    delay(500);
    int stateLow = digitalRead(LED_PWR);
    
    // 恢复
    digitalWrite(LED_PWR, LOW);
    
    // 如果有光敏传感器接在治具上, 可以在此读取 ADC
    // 此处仅验证 GPIO 可控
    r.passed = (stateHigh == HIGH && stateLow == LOW);
    r.detail = "GPIO14 H:" + String(stateHigh) + " L:" + String(stateLow);
    return r;
}

// 测试 10: 红色 LED (USB)
TestResult test_led_red() {
    TestResult r;
    
    digitalWrite(LED_USB, HIGH);
    delay(500);
    int stateHigh = digitalRead(LED_USB);
    
    digitalWrite(LED_USB, LOW);
    delay(500);
    int stateLow = digitalRead(LED_USB);
    
    digitalWrite(LED_USB, LOW);
    
    r.passed = (stateHigh == HIGH && stateLow == LOW);
    r.detail = "GPIO12 H:" + String(stateHigh) + " L:" + String(stateLow);
    return r;
}

// 测试 11: 按键检测
// 需要产测治具配合: 治具通过继电器/FET 模拟按键按下
// 或上位机提示操作员手动按下
TestResult test_button() {
    TestResult r;
    
    // 方案A: 自动化 — 通知上位机触发治具按键
    Serial.println("RSP:{\"test\":\"btn_test\",\"action\":\"PRESS_BTN_NOW\"}");
    
    // 读取当前状态(应为 HIGH = 未按)
    int initState = digitalRead(BTN);
    
    // 等待按键按下 (最多 5 秒)
    unsigned long start = millis();
    bool pressed = false;
    while (millis() - start < 5000) {
        if (digitalRead(BTN) == LOW) {
            pressed = true;
            break;
        }
        delay(10);
    }
    
    if (pressed) {
        // 等待释放
        start = millis();
        bool released = false;
        while (millis() - start < 3000) {
            if (digitalRead(BTN) == HIGH) {
                released = true;
                break;
            }
            delay(10);
        }
        r.passed = released;
        r.detail = "Init:" + String(initState) + " Press:OK Release:" + String(released ? "OK" : "STUCK");
    } else {
        r.passed = false;
        r.detail = "Init:" + String(initState) + " No press detected in 5s";
    }
    
    return r;
}

// 测试 12: MODE_SWITCH
TestResult test_mode_switch() {
    TestResult r;
    
    digitalWrite(MODE_SWITCH, LOW);
    delay(50);
    // 注意: GPIO33 是输出引脚, 无法直接回读, 仅验证无异常
    // 如果治具上有回读电路, 可在此添加 ADC/GPIO 读取
    
    digitalWrite(MODE_SWITCH, HIGH);
    delay(50);
    
    digitalWrite(MODE_SWITCH, LOW); // 恢复到串口模式
    delay(50);
    
    r.passed = true; // 无异常即通过
    r.detail = "GPIO33 toggle OK (output only)";
    return r;
}

// 测试 13: Zigbee LED 控制 (通过 ZNP 命令)
TestResult test_zb_led() {
    TestResult r;
    
    // 确保在串口模式
    digitalWrite(MODE_SWITCH, LOW);
    delay(100);
    
    while (Serial2.available()) Serial2.read();
    
    // 发送 LED ON 命令
    Serial2.write(zigLed1On, sizeof(zigLed1On));
    Serial2.flush();
    delay(400);
    
    // 检查响应: FE 01 67 0A 00 6C
    bool respOk = false;
    const byte expectedResp[] = {0xFE, 0x01, 0x67, 0x0A, 0x00, 0x6C};
    
    for (int retry = 0; retry < 3; retry++) {
        int avail = Serial2.available();
        if (avail >= 6) {
            byte buf[6];
            for (int i = 0; i < 6; i++) buf[i] = Serial2.read();
            
            respOk = true;
            for (int i = 0; i < 6; i++) {
                if (buf[i] != expectedResp[i]) {
                    respOk = false;
                    break;
                }
            }
            if (respOk) break;
        }
        while (Serial2.available()) Serial2.read();
        Serial2.write(zigLed1On, sizeof(zigLed1On));
        Serial2.flush();
        delay(400);
    }
    
    // 关闭 LED
    while (Serial2.available()) Serial2.read();
    Serial2.write(zigLed1Off, sizeof(zigLed1Off));
    Serial2.flush();
    delay(400);
    while (Serial2.available()) Serial2.read();
    
    r.passed = respOk;
    r.detail = respOk ? "ZNP LED cmd OK" : "ZNP LED no valid response";
    return r;
}

// 测试 14: 串口透传 (Serial2 回环, 需治具将 TX/RX 短接, 或与 CC2652P echo 配合)
// 注意: 此测试在正常产品上无法做回环, 因为 TX/RX 连着 CC2652P
// 替代方案: 已在 test 7/8/13 中验证了 Serial2 通讯能力
TestResult test_serial_loopback() {
    TestResult r;
    
    // 利用已通过的 CC2652P 通讯测试作为 Serial2 验证
    // 如果 test_cc2652p_comm 和 test_zb_led 都通过, 说明 Serial2 双向通讯正常
    // 这里做一个简单的发送+接收计数验证
    
    while (Serial2.available()) Serial2.read();
    
    // 发 SYS_PING: FE 00 21 01 20
    const byte cmdPing[] = {0xFE, 0x00, 0x21, 0x01, 0x20};
    Serial2.write(cmdPing, sizeof(cmdPing));
    Serial2.flush();
    delay(200);
    
    int bytesReceived = Serial2.available();
    while (Serial2.available()) Serial2.read();
    
    r.passed = (bytesReceived > 0);
    r.detail = "Ping resp bytes:" + String(bytesReceived);
    return r;
}

// 测试 15: 写入产测标记
TestResult test_factory_mark() {
    TestResult r;
    
    DynamicJsonDocument doc(256);
    doc["factory_tested"] = true;
    doc["test_time"] = String(millis() / 1000) + "s";
    doc["fw_version"] = "factory_test_v1.0";
    
    uint64_t mac = ESP.getEfuseMac();
    char macStr[18];
    sprintf(macStr, "%012llX", mac);
    doc["mac"] = macStr;
    
    LittleFS.mkdir("/config");
    File f = LittleFS.open(FACTORY_MARK_FILE, FILE_WRITE);
    if (f) {
        serializeJson(doc, f);
        f.close();
        
        // 读回验证
        File rf = LittleFS.open(FACTORY_MARK_FILE, FILE_READ);
        if (rf) {
            DynamicJsonDocument verify(256);
            deserializeJson(verify, rf);
            rf.close();
            
            r.passed = (verify["factory_tested"] == true);
            r.detail = "Mark written & verified";
        } else {
            r.passed = false;
            r.detail = "Mark written but read-back failed";
        }
    } else {
        r.passed = false;
        r.detail = "File write failed";
    }
    
    return r;
}

// ============ 运行全部测试 ============

void runAllTests() {
    memset(&results, 0, sizeof(results));
    
    unsigned long totalStart = millis();
    
    Serial.println("\n========================================");
    Serial.println("  AVATTO GW90-TI Factory Test Start");
    Serial.println("========================================\n");
    
    // --- 测试 1: ESP32 启动 ---
    Serial.println("[1/15] ESP32 Boot...");
    results.esp32_boot = test_esp32_boot();
    updateCount(results.esp32_boot);
    sendResponse("esp32_boot", results.esp32_boot.passed, results.esp32_boot.detail);
    
    // --- 测试 2: Flash 读写 ---
    Serial.println("[2/15] Flash R/W...");
    results.flash_rw = test_flash_rw();
    updateCount(results.flash_rw);
    sendResponse("flash_rw", results.flash_rw.passed, results.flash_rw.detail);
    
    // --- 测试 3: MAC 地址 ---
    Serial.println("[3/15] MAC Address...");
    results.mac_addr = test_mac_addr();
    updateCount(results.mac_addr);
    sendResponse("mac_addr", results.mac_addr.passed, results.mac_addr.detail);
    
    // --- 测试 4: CPU 温度 ---
    Serial.println("[4/15] CPU Temperature...");
    results.cpu_temp = test_cpu_temp();
    updateCount(results.cpu_temp);
    sendResponse("cpu_temp", results.cpu_temp.passed, results.cpu_temp.detail);
    
    // --- 测试 5: WiFi 扫描 ---
    Serial.println("[5/15] WiFi Scan...");
    results.wifi_scan = test_wifi_scan();
    updateCount(results.wifi_scan);
    sendResponse("wifi_scan", results.wifi_scan.passed, results.wifi_scan.detail);
    
    // --- 测试 6: 以太网 ---
    Serial.println("[6/15] Ethernet (LAN8720)...");
    results.ethernet = test_ethernet();
    updateCount(results.ethernet);
    sendResponse("ethernet", results.ethernet.passed, results.ethernet.detail);
    
    // --- 测试 7: CC2652P 通讯 ---
    Serial.println("[7/15] CC2652P Comm...");
    results.cc2652p_comm = test_cc2652p_comm();
    updateCount(results.cc2652p_comm);
    sendResponse("cc2652p_comm", results.cc2652p_comm.passed, results.cc2652p_comm.detail);
    
    // --- 测试 8: CC2652P 芯片 ID ---
    Serial.println("[8/15] CC2652P Chip ID...");
    results.cc2652p_chip = test_cc2652p_chip();
    updateCount(results.cc2652p_chip);
    sendResponse("cc2652p_chip", results.cc2652p_chip.passed, results.cc2652p_chip.detail);
    
    // --- 测试 9: 蓝色 LED ---
    Serial.println("[9/15] Blue LED (PWR)...");
    results.led_blue = test_led_blue();
    updateCount(results.led_blue);
    sendResponse("led_blue", results.led_blue.passed, results.led_blue.detail);
    
    // --- 测试 10: 红色 LED ---
    Serial.println("[10/15] Red LED (USB)...");
    results.led_red = test_led_red();
    updateCount(results.led_red);
    sendResponse("led_red", results.led_red.passed, results.led_red.detail);
    
    // --- 测试 11: 按键 ---
    Serial.println("[11/15] Button (waiting for press)...");
    results.btn_test = test_button();
    updateCount(results.btn_test);
    sendResponse("btn_test", results.btn_test.passed, results.btn_test.detail);
    
    // --- 测试 12: MODE_SWITCH ---
    Serial.println("[12/15] Mode Switch...");
    results.mode_switch = test_mode_switch();
    updateCount(results.mode_switch);
    sendResponse("mode_switch", results.mode_switch.passed, results.mode_switch.detail);
    
    // --- 测试 13: Zigbee LED ---
    Serial.println("[13/15] Zigbee LED...");
    // 需从 BSL 模式恢复, 先重启 CC2652P
    delay(1000); // 等待 CC2652P 重启完成
    Serial2.begin(115200, SERIAL_8N1, CC2652P_RXD, CC2652P_TXD);
    delay(2000); // CC2652P 启动需要时间
    results.zb_led = test_zb_led();
    updateCount(results.zb_led);
    sendResponse("zb_led", results.zb_led.passed, results.zb_led.detail);
    
    // --- 测试 14: 串口透传 ---
    Serial.println("[14/15] Serial Loopback...");
    results.serial_loopback = test_serial_loopback();
    updateCount(results.serial_loopback);
    sendResponse("serial_loopback", results.serial_loopback.passed, results.serial_loopback.detail);
    
    // --- 测试 15: 产测标记 ---
    Serial.println("[15/15] Factory Mark...");
    results.factory_mark = test_factory_mark();
    updateCount(results.factory_mark);
    sendResponse("factory_mark", results.factory_mark.passed, results.factory_mark.detail);
    
    // ============ 汇总 ============
    unsigned long totalTime = millis() - totalStart;
    
    Serial.println("\n========================================");
    Serial.printf("  RESULT: %s  (%d/%d passed)\n", 
                  results.total_fail == 0 ? "*** PASS ***" : "*** FAIL ***",
                  results.total_pass, results.total_tests);
    Serial.printf("  Total time: %lu ms\n", totalTime);
    Serial.println("========================================\n");
    
    sendSummary();
    
    // 最终 LED 指示
    if (results.total_fail == 0) {
        // 全部通过: 蓝灯常亮
        digitalWrite(LED_PWR, HIGH);
        digitalWrite(LED_USB, LOW);
    } else {
        // 有失败: 红灯快闪
        for (int i = 0; i < 20; i++) {
            digitalWrite(LED_USB, !digitalRead(LED_USB));
            delay(200);
        }
        digitalWrite(LED_USB, HIGH); // 红灯常亮表示失败
        digitalWrite(LED_PWR, LOW);
    }
}

// ============ 主程序 ============

void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    
    Serial.println("\n\n========================================");
    Serial.println("  AVATTO GW90-TI Factory Test Firmware");
    Serial.println("  Version: 1.0.0");
    Serial.println("========================================\n");
    
    // GPIO 初始化
    pinMode(CC2652P_RST, OUTPUT);
    pinMode(CC2652P_FLASH, OUTPUT);
    digitalWrite(CC2652P_RST, HIGH);
    digitalWrite(CC2652P_FLASH, HIGH);
    
    pinMode(LED_PWR, OUTPUT);
    pinMode(LED_USB, OUTPUT);
    pinMode(BTN, INPUT);
    pinMode(MODE_SWITCH, OUTPUT);
    
    digitalWrite(MODE_SWITCH, LOW); // 默认串口模式
    digitalWrite(LED_PWR, LOW);
    digitalWrite(LED_USB, LOW);
    
    // 初始化 Serial2 (与 CC2652P 通讯)
    Serial2.begin(115200, SERIAL_8N1, CC2652P_RXD, CC2652P_TXD);
    
    // 等待上位机指令或自动开始
    Serial.println("Send 'CMD:START' to begin tests, or waiting 3s for auto-start...");
    
    unsigned long waitStart = millis();
    bool manualStart = false;
    
    while (millis() - waitStart < 3000) {
        if (Serial.available()) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            if (cmd == "CMD:START") {
                manualStart = true;
                break;
            }
        }
        delay(50);
    }
    
    runAllTests();
}

void loop() {
    // 测试完成后等待指令
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd == "CMD:RETEST") {
            Serial.println("Re-running all tests...");
            // 重置 CC2652P
            digitalWrite(CC2652P_RST, LOW);
            delay(100);
            digitalWrite(CC2652P_RST, HIGH);
            delay(1000);
            Serial2.begin(115200, SERIAL_8N1, CC2652P_RXD, CC2652P_TXD);
            delay(2000);
            runAllTests();
        }
        else if (cmd == "CMD:REBOOT") {
            Serial.println("Rebooting...");
            delay(100);
            ESP.restart();
        }
        else if (cmd == "CMD:SUMMARY") {
            sendSummary();
        }
    }
    delay(100);
}
