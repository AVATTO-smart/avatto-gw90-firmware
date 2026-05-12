#include <Arduino.h>



void handleEvents();

//web服务器初始化：静态资源服务，页面路由，API接口，文件操作，固件升级
void initWebServer();                   

/*
//页面动态渲染

作用：为前端提供页面所需的 JSON 数据（通过 HTTP Header 传递）
工作模式：
    1.前端请求 /api?action=0&page=X
    2.后端调用对应 handleXxx() 函数
    3.生成 JSON 数据 → 通过 respValuesArr Header 返回
    4.前端用 JS 填充到 HTML 模板
*/
//提供首页数据
void handleRoot();
//状态页面
void handleStatus();
//General 页面
void handleGeneral();
//Security 页面
void handleSecurity();
//WIFI 页面
void handleWifi();
//Ethernet 配置页面
void handleEther();
//MQTT 配置页面
void handleMqtt();
//WG 配置页面
void handleWg();
//系统工具 页面
void handleSysTools();
//about页面
void handleAbout();

//API 接口处理:API命令如下
/*
enum CMD_t {
    CMD_ZB_ROUTER_RECON, // 路由器重连
    CMD_ZB_RST,          // Zigbee 重启
    CMD_ZB_BSL,          // 进入 Bootloader 模式
    CMD_ESP_RES,         // ESP32 重启
    CMD_ADAP_LAN,        // 切换 LAN 模式
    CMD_ADAP_USB,        // 切换 USB 模式
    CMD_LED_PWR_TOG,     // 电源 LED 切换
    CMD_LED_USB_TOG,     // USB/Wi-Fi LED 切换
    CMD_CLEAR_LOG,       // 清除日志
    CMD_ESP_UPD_URL,     // ESP32 在线升级
    CMD_ZB_CHK_REV,      // 检查 Zigbee 版本
    CMD_ZB_CHK_CON,      // 检查 Zigbee 连接
    CMD_ZB_LED_TOG       // Zigbee LED 切换
};
*/
void handleApi();

//保存表单数据
/*
作用：处理表单提交，将配置写入 LittleFS
工作流程：
    1.前端 POST /saveParams + 表单数据
    2.根据 pageId 确定配置类型
    3.读取现有配置 → 更新字段 → 写回文件
    4.调用 loadConfigXxx() 使配置生效
*/
void handleSaveParams();

//事件流通知
/*
作用：通过 Server-Sent Events (SSE) 实时推送进度
使用场景：
    1.固件升级进度 (ZB_FW_downloading)
    2.错误通知 (ZB_FW_err)
    3.ESP32 升级进度 (ESP_FW_prgs)
*/
void sendEvent(const char *event, const uint8_t evsz, const String data);

//处理client各种请求
void webServerHandleClient();
//zigbee进入BSL模式
void handleZigbeeBSL();
//zigbee重启
void handleZigbeeRestart();
//zigbee设置串口
void handleSerial();
//文件上传
void handleSavefile();

//静态资源页面加载
void sendGzip(const char* contentType, const uint8_t content[], uint16_t contentLen);

//打印日志时间
void printLogTime();
void printLogMsg(String msg);

//验证用户权限
bool checkAuth();

//升级进度处理
void progressFunc(unsigned int progress, unsigned int total);
//ESP32升级
void getEspUpdate(String esp_fw_url);
//开启ESP32升级
void runEspUpdateFirmware(uint8_t *data, size_t len);

#define UPD_FILE "https://github.com/AVATTO-smart/avatto-gw90-firmware/releases/latest/download/AVATTO-GW90-Ti.bin"