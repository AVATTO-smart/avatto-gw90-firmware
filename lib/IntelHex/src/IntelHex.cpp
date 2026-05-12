#include "IntelHex.h"
#include <esp_task_wdt.h>

/*
 * IntelHex Arduino Library
 *
 * Description:
 * This library is designed to read and parse Intel Hex format files from the LittleFS file system on ESP8266 and ESP32.
 *
 * License:
 * MIT License
 *
 * Author:
 * OpenAI's ChatGPT with contributions from xyzroe
 */
/*

If you want library to control LittleFS you need to uncomment some lines.

*/



#ifndef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(String(x))
#define DEBUG_PRINTLN(x) Serial.println(String(x))
#else
#define DEBUG_PRINT(x) Serial.print(String(x))
#define DEBUG_PRINTLN(x) Serial.println(String(x))
#endif
#endif

IntelHex::IntelHex(const char *filename) : _filename(filename) {}

IntelHex::~IntelHex()
{
    if (_file)
    {
        _file.close();
    }
}

bool IntelHex::open()
{
    // if (!LittleFS.begin()) {
    //     Serial.println("Could not mount file system");
    //     return false;
    // }
    _file = LittleFS.open(_filename, "r");
    return _file;
}

void IntelHex::close()
{
    _file.close();
    // LittleFS.end();
}

bool IntelHex::parse(void (*preCallback)(), void (*parseCallback)(uint32_t address, uint8_t len, uint8_t *data, size_t currentPosition, size_t totalSize), void (*postCallback)())
{
    if (!open())
    {
        return false;
    }

    _totalSize = _file.size();
    DEBUG_PRINTLN(String("_file.size() = ") + String(_totalSize));

    _bin_max_address = 0;
    _bin_extended_addr = 0;

    preCallback();

    bool status = true;

    while (status == true && _file.available() && _file_last_line == false)
    {
        status = _munchLine(parseCallback);
    }

    if (_file_last_line)
    {
        DEBUG_PRINTLN("HEX file last line found");
        _file_parsed = true;
    }
    else
    {
        DEBUG_PRINTLN("HEX file last line NOT found");
        status = false;
    }

    postCallback();

    close();
    return status;
}

/*
bool IntelHex::validateChecksum()
{
    DEBUG_PRINTLN("Starting checksum validation");

    if (!open())
    {
        DEBUG_PRINTLN("Failed to open file");
        return false;
    }

    String line;
    while (_file.available())
    {
        line = _file.readStringUntil('\n');
        line.trim();

        if (line.length() < 11 || line[0] != ':')
        {
            DEBUG_PRINTLN("Skipping non-data line or line too short");
            continue;
        }

        // Проверяем на строку окончания файла Intel HEX
        if (line == ":00000001FF")
        {
            DEBUG_PRINTLN("Intel HEX file end line found, checksum validation passed");
            close();
            return true;
        }

        uint8_t sum = 0;
        // Считаем сумму всех байтов строки, за исключением начального ':'
        for (int i = 1; i < line.length(); i += 2)
        {
            uint8_t byteValue = strtol(line.substring(i, i + 2).c_str(), nullptr, 16);
            sum += byteValue;
        }

        // Проверка контрольной суммы
        if (sum != 0)
        {
            DEBUG_PRINTLN("Checksum validation failed");
            close();
            return false;
        }
    }

    DEBUG_PRINTLN("Checksum validation passed, but no end line found - file might be incomplete");
    close();
    return false; // Если мы достигли этой точки, значит, строка окончания файла не была найдена.
}
*/

/*
bool IntelHex::checkBSLConfiguration() {
    DEBUG_PRINTLN("checkBSLConfiguration: Starting BSL configuration check");

    if (!open()) {
        DEBUG_PRINTLN("checkBSLConfiguration: Failed to open file");
        return false;
    }

    const uint32_t addresses[] = {ALL_CHIP_ADDRESS, P7_CHIP_ADDRESS};
    bool foundBSL = false;

    for (auto address : addresses) {
        DEBUG_PRINT("checkBSLConfiguration: Checking address: ");
        DEBUG_PRINTLN(String(address, HEX));

        // Предполагаем, что BSL конфигурация начинается непосредственно с этого адреса.
        // Важно отметить, что в файле Intel Hex адреса не обязательно идут последовательно и могут быть разбросаны.
        // Это может потребовать специфической логики для перемещения по файлу.

        // Чтение данных из файла, предполагая, что они могут находиться по этому адресу.
        _file.seek(address, SeekSet);
        uint8_t data[4]; // Предполагаем, что для проверки BSL конфигурации достаточно 4 байтов.
        if (_file.read(data, 4) != 4) {
            DEBUG_PRINTLN("checkBSLConfiguration: Failed to read data");
            continue; // Если не удается прочитать 4 байта, переходим к следующему адресу.
        }

        // Добавляем логирование прочитанных данных, чтобы увидеть, что было считано.
        DEBUG_PRINT("checkBSLConfiguration: Read data: ");
        for (int i = 0; i < 4; ++i) {
            DEBUG_PRINT(String(data[i], HEX));
            DEBUG_PRINT(" ");
        }
        DEBUG_PRINTLN();

        foundBSL = _checkBSLconfig(address, 4, data);
        if (foundBSL) {
            DEBUG_PRINTLN("checkBSLConfiguration: BSL configuration found and valid");
            break; // Если нашли и проверили конфигурацию BSL, прерываем цикл.
        }
    }

    if (!foundBSL) {
        DEBUG_PRINTLN("checkBSLConfiguration: BSL configuration not found or invalid");
    }

    close();
    return foundBSL;
}
*/

// bool IntelHex::_munchLine(void (*parseCallback)(uint32_t address, uint8_t len, uint8_t *data, size_t currentPosition, size_t totalSize))
// {

//     String line = _file.readStringUntil('\n');
//     line.trim();

//     //DEBUG_PRINTLN("Parsing line: " + line); // Print each processed line
//     //delay(1);

//     if (line.length() == 0 || line[0] != ':')
//     {
//         return true; // Continue parsing
//     }

//     uint8_t sum = 0;
//     for (int i = 1; i < line.length(); i += 2)
//     {
//         sum += strtol(line.substring(i, i + 2).c_str(), nullptr, 16);
//     }

//     if (sum != 0)
//     {
//         DEBUG_PRINTLN("Checksum line error");
//         return false;
//     }

//     if (line == ":00000001FF")
//     { // look like Intel HEX file end line
//         DEBUG_PRINTLN("File last line found");
//         _file_last_line = true;
//         return true;
//     }

//     uint8_t len = strtol(line.substring(1, 3).c_str(), nullptr, 16);
//     uint16_t offset_low = strtol(line.substring(3, 7).c_str(), nullptr, 16);
//     uint8_t recordType = strtol(line.substring(7, 9).c_str(), nullptr, 16);

//     //DEBUG_PRINTLN("Record Type: " + String(recordType, HEX) + ", Offset Low: " + String(offset_low, HEX) + ", Length: " + String(len, HEX)); // Print record info

//     uint8_t data[100]={0};
//     uint32_t address = offset_low;
//     // https://jimmywongiot.com/2021/04/20/format-of-IntelHex/
//     if (recordType == 4)
//     {
//         _offset_high = ((uint32_t)strtol(line.substring(9, 13).c_str(), nullptr, 16)) << 16; // because 4
//     }
//     else if (recordType == 2)
//     {
//         _offset_high = ((uint32_t)strtol(line.substring(9, 13).c_str(), nullptr, 16)) << 4; // because 2
//     }

//     address += _offset_high;

//     esp_task_wdt_reset();
// //#ifdef DEBUG
// //    delay(5); // to avoid reboot
// //#endif

//     for (int i = 0; i < ELEMENTCOUNT(CCFG_ADDRESS); i++)
//     {
//         // DEBUG_PRINT(CCFG_ADDRESS[i]);
//         // DEBUG_PRINT(" ");
//         if (address <= CCFG_ADDRESS[i] && address + len > CCFG_ADDRESS[i] + 4)
//         {
//             //DEBUG_PRINTLN(" ");
//             //DEBUG_PRINTLN("CCFG_ADDRESS[" + String(i) + "] in range");

            
//             for (uint8_t i = 0; i < len; i++)
//             {
//                 data[i] = strtol(line.substring(9 + i * 2, 11 + i * 2).c_str(), nullptr, 16);
//             }

//             if (!_bsl_valid)
//             {
//                 _bsl_valid = _checkBSLconfig(address, len, data);
//             }
//         }
//     }

//     size_t currentPosition = _file.position();

//     parseCallback(address, len, data, currentPosition, _totalSize);

//     return true;
// }


bool IntelHex::_munchLine(void (*parseCallback)(uint32_t address, uint8_t len, uint8_t *data, size_t currentPosition, size_t totalSize))
{
    String line = _file.readStringUntil('\n');
    line.trim();

    if (line.length() == 0 || line[0] != ':')
    {
        return true; // Continue parsing
    }

    // === 第一步：校验行完整性 ===
    uint8_t sum = 0;
    for (int i = 1; i < line.length(); i += 2)
    {
        sum += strtol(line.substring(i, i + 2).c_str(), nullptr, 16);
    }
    if (sum != 0)
    {
        DEBUG_PRINTLN("Checksum line error");
        return false;
    }

    // === 第二步：处理文件结束符 ===
    if (line == ":00000001FF")
    {
        DEBUG_PRINTLN("File last line found");
        _file_last_line = true;
        return true;
    }

    // === 第三步：统一解析记录 ===
    uint8_t dataLen = strtol(line.substring(1, 3).c_str(), nullptr, 16);
    uint16_t addressLow = strtol(line.substring(3, 7).c_str(), nullptr, 16);
    uint8_t recordType = strtol(line.substring(7, 9).c_str(), nullptr, 16);

    // 初始化当前记录的完整地址
    uint32_t fullAddress = addressLow;

    // 根据记录类型更新全局偏移量 [_offset_high](file://e:\Project\PROE\GitCode\PREGW90\lib\IntelHex\src\IntelHex.h#L36-L36)
    if (recordType == 4) // 扩展线性地址记录
    {
        _offset_high = ((uint32_t)strtol(line.substring(9, 13).c_str(), nullptr, 16)) << 16;
    }
    else if (recordType == 2) // 扩展段地址记录
    {
        _offset_high = ((uint32_t)strtol(line.substring(9, 13).c_str(), nullptr, 16)) << 4;
    }

    // 应用全局偏移量得到最终地址
    fullAddress += _offset_high;

    // === 第四步：为 BIN 大小计算同步更新状态 ===
    // 使用独立的状态变量 _bin_extended_addr
    if (recordType == 0x02) { // 扩展段地址记录
        _bin_extended_addr = ((uint32_t)strtol(line.substring(9, 13).c_str(), nullptr, 16)) << 4;
    } else if (recordType == 0x04) { // 扩展线性地址记录
        _bin_extended_addr = ((uint32_t)strtol(line.substring(9, 13).c_str(), nullptr, 16)) << 16;
    }

    if (recordType == 0x00) { // 数据记录
        uint32_t binFullAddress = _bin_extended_addr + addressLow;
        uint32_t endAddress = binFullAddress + dataLen;
        if (endAddress > _bin_max_address) {
            _bin_max_address = endAddress;
        }
    }
    // 注意: 对于 0x02 和 0x04 记录，[_offset_high](file://e:\Project\PROE\GitCode\PREGW90\lib\IntelHex\src\IntelHex.h#L36-L36) 已经在上面更新，
    // 因此 [_bin_extended_addr](file://e:\Project\PROE\GitCode\PREGW90\lib\IntelHex\src\IntelHex.h#L46-L46) 的逻辑被 [_offset_high](file://e:\Project\PROE\GitCode\PREGW90\lib\IntelHex\src\IntelHex.h#L36-L36) 完全替代，无需额外处理。

    // === 第五步：提取数据并执行回调 ===
    uint8_t data[100] = {0};
    for (uint8_t i = 0; i < dataLen && i < sizeof(data); i++)
    {
        data[i] = strtol(line.substring(9 + i * 2, 11 + i * 2).c_str(), nullptr, 16);
    }

    // 检查 BSL 配置 (使用统一的 `fullAddress`)
    for (int i = 0; i < ELEMENTCOUNT(CCFG_ADDRESS); i++)
    {
        if (fullAddress <= CCFG_ADDRESS[i] && fullAddress + dataLen > CCFG_ADDRESS[i] + 4)
        {
            if (!_bsl_valid)
            {
                _bsl_valid = _checkBSLconfig(fullAddress, dataLen, data);
            }
        }
    }

    // 调用用户回调 (使用统一的 `fullAddress`)
    size_t currentPosition = _file.position();
    parseCallback(fullAddress, dataLen, data, currentPosition, _totalSize);

    esp_task_wdt_reset();
    return true;
}

bool IntelHex::_checkBSLconfig(uint32_t address, uint8_t len, uint8_t *data)
{
    // Check if CCFG is within the buffer
    // DEBUG_PRINTLN(" ");
    // DEBUG_PRINT(ELEMENTCOUNT(CCFG_ADDRESS));
    // DEBUG_PRINT(" ");

    for (int i = 0; i < ELEMENTCOUNT(CCFG_ADDRESS); i++)
    {
        // DEBUG_PRINT(CCFG_ADDRESS[i]);
        // DEBUG_PRINT(" ");
        if (address <= CCFG_ADDRESS[i] && address + len > CCFG_ADDRESS[i] + 4)
        {
            DEBUG_PRINTLN(" ");
            DEBUG_PRINTLN("CCFG_ADDRESS[" + String(i) + "] in range");

            if (data[CCFG_ADDRESS[i] - address + 3] == BOOTLOADER_ENABLE)
            {
                DEBUG_PRINTLN("Bootloader enabled");
                _bsl_bootloader_enbl = true;
            }

            if (data[CCFG_ADDRESS[i] - address + 0] == BL_ENABLE)
            {
                DEBUG_PRINTLN("'failure analysis' enabled");
                _bsl_bl_enbl = true;
            }

            if (data[CCFG_ADDRESS[i] - address + 2] == BL_LEVEL_LOW)
            {
                DEBUG_PRINTLN("BSL low level");
                _bsl_level = 1;
            }
            else if (data[CCFG_ADDRESS[i] - address + 2] == BL_LEVEL_HIGH)
            {
                DEBUG_PRINTLN("BSL high level");
                _bsl_level = 2;
            }
            else
            {
                DEBUG_PRINTLN("BSL level UNKNOWN. Error!");
                _bsl_level = 0;
            }

            // Pin in HEX converts to DEC number
            _bsl_pin = data[CCFG_ADDRESS[i] - address + 1];
            DEBUG_PRINTLN("BSL pin - " + String(_bsl_pin));

            if (_bsl_bootloader_enbl && _bsl_bl_enbl && (_bsl_level > 0) && (_bsl_pin > 0))
            {
                DEBUG_PRINTLN("BSL valid!");
                _bsl_addr = i;
                return true;
            }
        }
    }
    return false;
}
