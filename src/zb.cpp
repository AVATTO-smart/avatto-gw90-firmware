#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <FS.h>
#include <LittleFS.h>
#include <ETH.h>
#include <HTTPClient.h>
#include <esp_task_wdt.h>

#include <IntelHex.h>
#include <CCTools.h>

#include "config.h"
#include "web.h"
#include "log.h"
#include "etc.h"
#include "zb.h"

extern struct ConfigSettingsStruct ConfigSettings;
extern struct zbVerStruct zbVer;

extern const char *tempFile;

const byte cmdLed0 = 0x27;
const byte cmdLed1 = 0x0A;
const byte cmdLedIndex = 0x01; // for led 1
const byte cmdLedStateOff = 0x00;
const byte cmdLedStateOn = 0x01;
const byte cmdFrameStart = 0xFE;
const byte cmdLedLen = 0x02;

const byte zigLed1Off[] = {cmdFrameStart, cmdLedLen, cmdLed0, cmdLed1, cmdLedIndex, cmdLedStateOff, 0x2E}; // resp FE 01 67 0A 00 6C
const byte zigLed1On[] = {cmdFrameStart, cmdLedLen, cmdLed0, cmdLed1, cmdLedIndex, cmdLedStateOn, 0x2F};
const byte cmdLedResp[] = {0xFE, 0x01, 0x67, 0x0A, 0x00, 0x6C};

size_t lastSize = 0;

CCTools CCTool(Serial2, CC2652P_RST, CC2652P_FLASH);

volatile size_t zbFlashTotalSent = 0;
volatile size_t zbFlashTotalSize = 0;

bool bslActive;



bool zbCheckLastCmd(void);


void clearS2Buffer()
{
    while (Serial2.available())
    { // clear buffer
        Serial2.read();
    }
}


//协议版本   产品ID       版本        20250321
//  02        01      02 07 01     D1 FE 34 01   00     74(校验)
void getZbVer()
{
    zbVer.zbRev = 0;
    const byte cmdFrameStart = 0xFE;
    const byte zero = 0x00;
    const byte cmd1 = 0x21;
    const byte cmd2 = 0x02;
    const byte cmdSysVersion[] = {cmdFrameStart, zero, cmd1, cmd2, 0x23};


    for (uint8_t i = 0; i < 6; i++)
    {
        if (Serial2.read() != cmdFrameStart || Serial2.read() != 0x0a || Serial2.read() != 0x61 || Serial2.read() != cmd2)
        {                    // check for packet start
            clearS2Buffer(); // skip
            Serial2.write(cmdSysVersion, sizeof(cmdSysVersion));
            Serial2.flush();
            delay(100);
        }
        else
        {
            const uint8_t zbVerLen = 11;
            byte zbVerBuf[zbVerLen];
            for (uint8_t i = 0; i < zbVerLen; i++)
            {
                zbVerBuf[i] = Serial2.read();
            }
            zbVer.zbRev = zbVerBuf[5] | (zbVerBuf[6] << 8) | (zbVerBuf[7] << 16) | (zbVerBuf[8] << 24);
            zbVer.maintrel = zbVerBuf[4];
            zbVer.minorrel = zbVerBuf[3];
            zbVer.majorrel = zbVerBuf[2];
            zbVer.product = zbVerBuf[1];
            zbVer.transportrev = zbVerBuf[0];

            Serial.write(zbVerBuf,zbVerLen);
            printLogMsg(String("[ZBVER]") + " Rev: " + zbVer.zbRev + " Maintrel: " + zbVer.maintrel + " Minorrel: " + zbVer.minorrel + " Majorrel: " + zbVer.majorrel + " Transportrev: " + zbVer.transportrev + " Product: " + zbVer.product);
            clearS2Buffer();
            break;
        }
    }
}

void zbCheck()
{
    // getZbChip();
    //  Serial2.begin(115200, SERIAL_8N1, CC2652P_RXD, CC2652P_TXD); //start zigbee serial
    bool respOk = false;
    for (uint8_t i = 0; i < 12; i++)
    { // wait for zigbee start
        if (respOk)
            break;
        clearS2Buffer();
        Serial2.write(zigLed1On, sizeof(zigLed1On));
        Serial2.flush();
        delay(400);
        for (uint8_t i = 0; i < 5; i++)
        {
            if (Serial2.read() != 0xFE)
            {                   // check for packet start
                Serial2.read(); // skip
            }
            else
            {
                for (uint8_t i = 1; i < 4; i++)
                {
                    if (Serial2.read() != cmdLedResp[i])
                    { // check if resp ok
                        respOk = false;
                        break;
                    }
                    else
                    {
                        respOk = true;
                    }
                }
            }
        }
        digitalWrite(LED_USB, !digitalRead(LED_USB)); // blue led flashing mean wait for zigbee resp
    }
    delay(500);
    if (!respOk)
    {
        digitalWrite(LED_PWR, 1);
        digitalWrite(LED_USB, 1);
        for (uint8_t i = 0; i < 5; i++)
        { // indicate wrong resp
            digitalWrite(LED_PWR, !digitalRead(LED_PWR));
            digitalWrite(LED_USB, !digitalRead(LED_USB));
            delay(1000);
        }
        printLogMsg("[ZBCHK] Wrong answer");
        printLogMsg("[ZBVER] Unknown");
        zbVer.zbRev = 0;
    }
    else
    {
        Serial2.write(zigLed1Off, sizeof(zigLed1Off));
        Serial2.flush();
        delay(250);
        clearS2Buffer();
        printLogMsg("[ZBCHK] Connection OK");
    }
    digitalWrite(LED_PWR, 0);
    digitalWrite(LED_USB, 0);
}

void zbLedToggle()
{
    bool respOk = false;
    clearS2Buffer();
    if (ConfigSettings.zbLedState == 0)
    {
        printLogMsg("[ZB] LED toggle ON");
        Serial2.write(zigLed1On, sizeof(zigLed1On));
    }
    else
    {
        printLogMsg("[ZB] LED toggle OFF");
        Serial2.write(zigLed1Off, sizeof(zigLed1Off));
    }
    Serial2.flush();
    delay(400);
    for (uint8_t i = 0; i < 5; i++)
    {
        if (Serial2.read() != 0xFE)
        {                   // check for packet start
            Serial2.read(); // skip
        }
        else
        {
            for (uint8_t i = 1; i < 4; i++)
            {
                if (Serial2.read() != cmdLedResp[i])
                { // check if resp ok
                    respOk = false;
                    break;
                }
                else
                {
                    respOk = true;
                }
            }
        }
    }
    if (respOk)
    {
        printLogMsg("[ZB] LED toggle OK");
        ConfigSettings.zbLedState = !ConfigSettings.zbLedState;
    }
}

void preParse()
{
    DEBUG_PRINTLN(String(millis()) + " Starting the parsing process");
}

void postParse()
{
    DEBUG_PRINTLN(String(millis()) + " Parsing complete");
}

void parseCallback(uint32_t address, uint8_t len, uint8_t *data, size_t currentPosition, size_t _totalSize)
{
    // DEBUG_PRINT(".");

     // Print each parsed record for debugging purposes
   /* DEBUG_PRINT("Address: 0x");
    DEBUG_PRINT(String(address, HEX));
    DEBUG_PRINT(", Length: 0x");
    DEBUG_PRINTLN(String(len, HEX));

    for (uint8_t i = 0; i < len; i++)
    {
        DEBUG_PRINT("0x");
        DEBUG_PRINT(String(data[i], HEX));
        DEBUG_PRINT(" ");
    }
    DEBUG_PRINTLN(""); */

    // if (currentPosition - lastSize > 12500)
    // {
    //     lastSize = currentPosition;
    //     float percent = ((float)currentPosition / _totalSize) * 100.0;
    //     sendEvent("ZB_FW_prgs", sizeof("ZB_FW_prgs"), String(percent));
    //     DEBUG_PRINTLN(String("ZB_FW_prgs") + String(" | ") + String(percent) + String("%"));
    // }
}

int8_t runFlash(const char *hexFilePath,uint32_t binsize)
{
    //const char *tempFile = "/config/fw.hex";
    File fwFile = LittleFS.open(tempFile, "r");
    if (!fwFile)
    {
        DEBUG_PRINTLN(F("Failed to open fw.hex file"));
        printLogMsg("[ZB_FLASH] Failed to open fw.hex file");
        return -1;
    }

    DEBUG_PRINTLN(F("Starting to send fw.hex data..."));
    printLogMsg("[ZB_FLASH] Starting to send fw.hex data...");

    uint32_t zbFlashTotalSize = binsize;
    uint32_t zbFlashTotalSent  = 0;

    if (zbFlashTotalSize  == 0)
    {
        printLogMsg("[ZB_FLASH] Failed to get total size");
        fwFile.close();
        return -1;
    }
    zbFlashStart(0,zbFlashTotalSize);

    size_t packetCount = 0;

    // 缓冲区，存储 240 字节数据
    byte dataBuffer[240] = {0};
    size_t totalBytes = 0;
    uint8_t lineCount = 0;
    
    // 地址跟踪变量
    uint32_t extendedAddr = 0;
    uint32_t currentWriteAddr = 0;  // 当前缓冲区中的写入地址
    uint32_t nextExpectAddr = 0;    // 下一条记录期望的地址
    bool firstRecord = true;
    uint8_t i=0;

    // 进度跟踪变量
    size_t lastProgressSize = 0;
    const size_t progressInterval = zbFlashTotalSize / 100;  // 每 12500 字节上报一次进度

    Serial.printf("fw flash size: %ld\n",zbFlashTotalSize);
    while (fwFile.available())
    {
        String line = fwFile.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0 && line[0] == ':')
        {
            // 跳过结束记录
            if (line == ":00000001FF")
            {
                DEBUG_PRINTLN("HEX file end record found");
                break;
            }
            
            // 解析记录类型
            uint8_t recordType = strtol(line.substring(7, 9).c_str(), nullptr, 16);
            // 处理扩展线性地址记录 (0x04)
            if (recordType == 0x04)
            {
                extendedAddr = strtol(line.substring(9, 13).c_str(), nullptr, 16) << 16;
                continue;
            }
            // 处理扩展段地址记录 (0x02)
            if (recordType == 0x02)
            {
                extendedAddr = strtol(line.substring(9, 13).c_str(), nullptr, 16) << 4;
                continue;
            }
            // 只处理数据记录（类型 00）
            if (recordType != 0x00)
            {
                continue;
            }
            // 解析当前行的地址和数据长度
            uint16_t lineAddr = strtol(line.substring(3, 7).c_str(), nullptr, 16);
            uint8_t lineDataLen = strtol(line.substring(1, 3).c_str(), nullptr, 16);
            uint32_t fullAddr = extendedAddr + lineAddr;
            // 第一条记录初始化
            if (firstRecord)
            {
                currentWriteAddr = fullAddr;
                nextExpectAddr = fullAddr;
                firstRecord = false;
                DEBUG_PRINTLN(String("[HEX] Start address: 0x") + String(fullAddr, HEX));
            }
            // 检查地址是否连续，如果不连续需要填充空白
            if (fullAddr != nextExpectAddr)
            {
                // 计算地址间隙大小
                uint32_t gapSize = fullAddr - nextExpectAddr;
                
                DEBUG_PRINT(String("[HEX] Gap detected: 0x") + String(nextExpectAddr, HEX));
                DEBUG_PRINT(" -> 0x" + String(fullAddr, HEX));
                DEBUG_PRINTLN(" (" + String(gapSize) + " bytes)");
                
                while (gapSize > 0)
                {
                    // 计算本次可以填充的数量（缓冲区剩余空间）
                    uint32_t fillSize = gapSize;
                    if (fillSize > (240 - totalBytes))
                    {
                        fillSize = 240 - totalBytes;
                    }
                    // 填充 0xFF
                    for (uint32_t i = 0; i < fillSize; i++)
                    {
                        dataBuffer[totalBytes + i] = 0xFF;
                    }
                    totalBytes += fillSize;
                    gapSize -= fillSize;
                    nextExpectAddr += fillSize;
                    
                    // 如果缓冲区满了，发送当前数据
                    if (totalBytes >= 240)
                    {
                        packetCount++;
                        zbFlashTotalSent += totalBytes;
                        
                        DEBUG_PRINT("Packet #");
                        DEBUG_PRINT(packetCount);
                        DEBUG_PRINT(" | Addr: 0x");
                        DEBUG_PRINT(String(currentWriteAddr, HEX));
                        DEBUG_PRINT(" | ");
                        DEBUG_PRINT(totalBytes);
                        DEBUG_PRINTLN(" bytes [GAP FILL]");
                        i=0;              
                        while(!zbCheckLastCmd())
                        {
                            i++;
                            if(i>10)goto flash_quit;
                        }
                        zbSendCommand(0x24, dataBuffer, totalBytes, 3);
                        
                        memset(dataBuffer, 0, sizeof(dataBuffer));
                        totalBytes = 0;
                        lineCount = 0;
                        currentWriteAddr = nextExpectAddr;
                        
                        if (zbFlashTotalSent - lastProgressSize >= progressInterval)
                        {
                            uint8_t percent = ((float)zbFlashTotalSent / zbFlashTotalSize) * 100;
                            sendEvent("ZB_FW_flashing", sizeof("ZB_FW_flashing"), String(percent));
                            lastProgressSize = zbFlashTotalSent;
                        }
                    }
                }
            }
            
            // 检查缓冲区是否能容纳当前行数据
            if (totalBytes + lineDataLen > 240)
            {
                // 缓冲区满了，先发送当前数据
                packetCount++;
                zbFlashTotalSent += totalBytes;
                
                DEBUG_PRINT("Packet #");
                DEBUG_PRINT(packetCount);
                DEBUG_PRINT(" | Addr: 0x");
                DEBUG_PRINT(String(currentWriteAddr, HEX));
                DEBUG_PRINT(" | ");
                DEBUG_PRINT(totalBytes);
                DEBUG_PRINT(" bytes (");
                DEBUG_PRINT(lineCount);
                DEBUG_PRINTLN(" lines)");
                
                i=0;              
                while(!zbCheckLastCmd())
                {
                    i++;
                    if(i>10)goto flash_quit;
                }
                zbSendCommand(0x24, dataBuffer, totalBytes, 3);
                
                // 清空缓冲区，更新起始地址
                memset(dataBuffer, 0, sizeof(dataBuffer));
                totalBytes = 0;
                lineCount = 0;
                currentWriteAddr = fullAddr;
                nextExpectAddr = fullAddr;
                

                if (zbFlashTotalSent - lastProgressSize >= progressInterval)
                {
                    uint8_t percent = ((float)zbFlashTotalSent / zbFlashTotalSize) * 100;
                    sendEvent("ZB_FW_flashing", sizeof("ZB_FW_flashing"), String(percent));
                    lastProgressSize = zbFlashTotalSent;
                }
            }
            
            // 解析当前行数据并写入缓冲区
            for (uint8_t i = 0; i < lineDataLen; i++)
            {
                char hexByte[3] = {0};
                hexByte[0] = line.charAt(9 + i * 2);
                hexByte[1] = line.charAt(9 + i * 2 + 1);
                dataBuffer[totalBytes + i] = strtol(hexByte, nullptr, 16);
            }
            
            totalBytes += lineDataLen;
            lineCount++;
            nextExpectAddr = fullAddr + lineDataLen; // 更新期望的下一个地址
        }
    }
    
    // 发送最后一帧剩余数据
    if (totalBytes > 0)
    {
        packetCount++;
        zbFlashTotalSent += totalBytes;
        
        DEBUG_PRINT("Packet #");
        DEBUG_PRINT(packetCount);
        DEBUG_PRINT(" | Addr: 0x");
        DEBUG_PRINT(String(currentWriteAddr, HEX));
        DEBUG_PRINT(" | ");
        DEBUG_PRINT(totalBytes);
        DEBUG_PRINT(" bytes (");
        DEBUG_PRINT(lineCount);
        DEBUG_PRINTLN(" lines) [LAST]");
        
        i=0;              
        while(!zbCheckLastCmd())
        {
            i++;
            if(i>10)goto flash_quit;
        }
        zbSendCommand(0x24, dataBuffer, totalBytes, 3);

        // 【新增】上报最后进度
        zbFlashTotalSent = zbFlashTotalSize;

        // ← 添加：发送烧录完成事件
        sendEvent("ZB_FW_flashing", sizeof("ZB_FW_flashing"), "100");
    }
    i=0;              
    while(!zbCheckLastCmd())
    {
        i++;
        if(i>10)break;
    }
flash_quit:
    fwFile.close();
    Serial2.updateBaudRate(ConfigSettings.serialSpeed); 
    CCTool.restart();         //重启
    DEBUG_PRINTLN(F("fw.hex data send complete"));
    Serial.printf("total send: %ld",zbFlashTotalSent);
    printLogMsg(String("[ZB_FLASH] fw.hex data send complete | Total: ") + String(zbFlashTotalSent) + " bytes");
    if(zbFlashTotalSent == zbFlashTotalSize)return 0;
    return -1;
}

int8_t checkFwHex(const char *tempFile) // check Zigbee FW file using IntelHEX, than check BSL pin.
{
    IntelHex zb_hex(tempFile);

    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    Serial.printf("[ZB_FLASH] Free heap before verify: %u bytes\n", (unsigned int)free_heap);

    if (!zb_hex.parse(preParse, parseCallback, postParse))
    {
        String msg = ("Failed to parse the zb_hex HEX file. Corrupted file");
        DEBUG_PRINTLN(msg);
        printLogMsg(msg);
    }

    if (!zb_hex.fileParsed())
    {
        String msg = ("File not good");
        DEBUG_PRINTLN(msg);
        printLogMsg(msg);
    }

    u_int8_t local_chip_id = ALL_CHIP_ID;
    if (zbVer.chipID == "CC2652P7")
        local_chip_id = P7_CHIP_ID;

    if (zb_hex.bslActive())
    {
        String msg = "BSL (" + String(zb_hex.bslAddr() ? "P7 and R7 chips" : "All chips") + ") pin " + String(zb_hex.bslPin()) + " level " + String(zb_hex.bslLevel() ? "HIGH" : "LOW");
        DEBUG_PRINTLN(msg);
        printLogMsg(msg);

        if (zb_hex.bslAddr() == local_chip_id && zb_hex.bslPin() == NEED_BSL_PIN && zb_hex.bslLevel() == NEED_BSL_LEVEL) // All series DIO 15 LOW
        {
            zb_hex.setFileValidated(true);
            String msg = ("BSL config OK");
            DEBUG_PRINTLN(msg);
            printLogMsg(msg);
        }
        else
        {
            String msg = ("BSL config incorect. Wrong chip, pin or level.");
            DEBUG_PRINTLN(msg);
            printLogMsg(msg);
        }
    }
    else
    {
        String msg = ("BSL config error. Range not found or CCFG incorrect.");
        DEBUG_PRINTLN(msg);
        printLogMsg(msg);
    }

    if (!zb_hex.fileValidated())
    {
        String msg = ("Zigbee FW file INVALID - ERROR");
        DEBUG_PRINTLN(msg);
        printLogMsg(msg);
        return -1;
    }
    else
    {
        String msg = ("Zigbee FW file VALID - OK");
        DEBUG_PRINTLN(msg);
        printLogMsg(msg);
        sendEvent("ZB_FW_info", sizeof("ZB_FW_info"), "Validation complete!");

        return runFlash(tempFile,zb_hex.getBinSize());
    }
    return -1;
}

void zbInit()
{

    // zbCheck();
    // getZbVer();
    if (CCTool.begin())
    {

        // CCTool.cmdGetChipId();
        String zb_chip = CCTool.detectChipInfo();
        DEBUG_PRINTLN(zb_chip);
        printLogMsg(String("[ZBCHIP] ") + zb_chip);
        zbVer.chipID = zb_chip;
        // zigbeeRestart();
        CCTool.restart();
        // delay(5000);

        // getZbVer();
    }
    else
    {
        String msg = "No connection with Zigbee";
        printLogMsg(String("[ZBCHIP] ") + msg);
        DEBUG_PRINTLN(msg);
    }
}



void zbBegin(void)
{
    CCTool.begin();
}

/************************************************************************** */

uint8_t zbGenChksum(uint8_t cmd, uint8_t *data, uint32_t len)
{
    uint8_t checksum = cmd;
    for (uint32_t i = 0; i < len; i++)
    {
        checksum += data[i];
    }
    return checksum;
}

bool zbWaitForAck(uint32_t timeout)
{
    uint32_t startMillis = millis();
    while (millis() - startMillis < timeout * 1000)
    {
        if (Serial2.available() >= 1)
        {
            uint8_t received = Serial2.read();
            //Serial.printf("zbWaitForAck: 0x%2x\n",received);
            if (received == 0xcc)
            {
                return true;
            }
            else if (received == 0x33)
            {
                return false;
            }
        }
    }
    return false;
}

bool zbSendCmdResponse(bool ack)
{
    uint8_t pData[2] = {0};
    pData[0] = 0x00;
    pData[1] = (ack) ? 0xCC : 0x33;

    if(Serial2.write(pData,2)!=2)return false;

    //Serial.println("zbSendCommand ack: " + String(ack ? "ACK" : "NACK"));

    return true;
}

bool zbSendCommand(uint8_t cmd, uint8_t *data, uint8_t len,uint8_t timeout)
{
    if(len > 252)return false;

    unsigned int i = 0;

    byte cmd_buf[len + 3]={0};

    cmd_buf[0] = len + 3;   
    cmd_buf[1] = zbGenChksum(cmd,data,len);
    cmd_buf[2] = cmd;

    for (i = 0; i < len; i++)
    {
        cmd_buf[i + 3] = data[i];
    }

    Serial2.write(cmd_buf, len + 3);
    //Serial.write(cmd_buf, len + 3);

    return zbWaitForAck(1);
}

uint32_t zbGetResponseData(uint8_t *data,uint8_t timeout)
{
    unsigned int i = 0;
    byte checksum = 0;

    uint32_t numPayloadBytes = 0;

    if(data==NULL)return 0;

    unsigned long startMillis = millis();

    while (millis() - startMillis < timeout * 1000)
    {
        if (Serial2.available() >= 1)
        {
            data[i++] = Serial2.read();
        }
        if(i == 2)break;
    }

    if(i < 2)return 0;

    numPayloadBytes = data[0] -2;
    checksum = data[1];

    //Serial.write(data,i);

    i = 0;
    while (millis() - startMillis < timeout * 1000)
    {
        if (Serial2.available() >= 1)
        {
            data[i++] = Serial2.read();
        }
        if(i == numPayloadBytes)break;
    }

    if(i < numPayloadBytes)return 0;

    // Serial.write(data,i+2);
    if(checksum != zbGenChksum(0,data,numPayloadBytes))
    {
        zbSendCmdResponse(false);
        return 0;
    }
    zbSendCmdResponse(true);
    return 1;
}

bool zbGetStatus(uint8_t *status,uint32_t timeout)
{
    if(status == NULL)return false;

    if(zbSendCommand(0x23,0,0,timeout))
    {
        if(zbGetResponseData(status,timeout))
        {
            return true;
        }
    }
    return false;
}

bool zbCheckLastCmd(void)
{
    uint8_t stat[10] = {0};
    if(zbGetStatus(stat,1))
    {
        //Serial.printf("zbCheckLastCmd: %02X\n",stat[0]);
        return (stat[0] == 0x40);
    }
    return false;
 }

 bool zbFlashStart(uint32_t addr,uint32_t flashSize)
 {
    uint8_t buf[8]={0};
    uint32_t size =0;

    size = flashSize; 

    buf[0] = (uint8_t)(addr >> 24) ;
    buf[1] = (uint8_t)(addr >> 16);
    buf[2] = (uint8_t)(addr >> 8);
    buf[3] = (uint8_t)(addr >> 0);

    buf[4] = (uint8_t)(size >> 24);
    buf[5] = (uint8_t)(size >> 16);
    buf[6] = (uint8_t)(size >> 8);
    buf[7] = (uint8_t)(size >> 0);

    Serial.printf("update baud\n");
    Serial2.updateBaudRate(1000000);         //提高波特率
    Serial.printf("erase flash\n");
    zbEraseFlash(0,size);

    Serial.printf("send start\n");
    if(zbSendCommand(0x21,buf,8,2))
    {
        if(zbGetStatus(buf,1))
        {
            //Serial.printf("flash read status: %02X\n",buf[0]);
            return (buf[0] == 0x40);
        }
    }
    return false;
 }

bool zbSendSync(uint32_t timeout)
{
    uint8_t buf[2]={0x55,0x55};
    Serial2.write(buf,2);
    return zbWaitForAck(timeout);
}


bool zbEraseFlash(uint32_t start,uint32_t size)
{
    if(!bslActive)
    {
        CCTool.enterBSL();
        bslActive = 1;
    }
    if(zbSendSync(3)) 
    {
        return zbSendCommand(0x2c,0,0,5);
    }
    return false;
}


size_t getHexBinSize(File hexFile)
{
    //File hexFile = LittleFS.open(hexFilePath, "r");
    if (!hexFile)
    {
        DEBUG_PRINTLN(F("Failed to open hex file"));
        printLogMsg("[HEX_SIZE] Failed to open hex file");
        return 0;
    }

    //return 360448;   //2652芯片固定bin大小

    uint32_t maxAddress = 0;
    uint32_t extendedAddr = 0;
    
    while (hexFile.available())
    {
        String line = hexFile.readStringUntil('\n');
        line.trim();
        
        if (line.length() > 0 && line[0] == ':')
        {
            // 跳过结束记录
            if (line == ":00000001FF")
            {
                break;
            }
            
            // 解析记录类型
            uint8_t recordType = strtol(line.substring(7, 9).c_str(), nullptr, 16);
            
            if (recordType == 0x00) // 数据记录
            {
                uint16_t address = strtol(line.substring(3, 7).c_str(), nullptr, 16);
                uint8_t dataLen = strtol(line.substring(1, 3).c_str(), nullptr, 16);
                uint32_t fullAddress = extendedAddr + address;
                uint32_t endAddress = fullAddress + dataLen;
                
                if (endAddress > maxAddress)
                {
                    maxAddress = endAddress;
                }
            }
            else if (recordType == 0x02) // 扩展段地址记录
            {
                extendedAddr = strtol(line.substring(9, 13).c_str(), nullptr, 16) << 4;
            }
            else if (recordType == 0x04) // 扩展线性地址记录
            {
                extendedAddr = strtol(line.substring(9, 13).c_str(), nullptr, 16) << 16;
            }
        }
    }
    
    //hexFile.close();
    
    DEBUG_PRINTLN(String("[HEX_SIZE] BIN size: ") + String(maxAddress) + " bytes");
    printLogMsg(String("[HEX_SIZE] ") + String(maxAddress) + " bytes");
    
    return maxAddress;
}

