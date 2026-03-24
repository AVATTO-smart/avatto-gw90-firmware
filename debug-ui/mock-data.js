/**
 * 模拟数据 - 用于调试 UI
 * 可以根据需要修改这些数据来测试不同的显示状态
 */

const mockData = {
    root: {
        // Device status
        operationalMode: 'LAN Mode',
        connectedEther: 'Yes',
        connectedSocketStatus: 'Connected (3 clients)',
        wifiEnabled: 'No',
        wifiConnected: 'Disconnected',
        wifiModeAP: 'No',
        wifiModeAPStatus: 'Disabled',
        uptime: '5 days 12:34:56',
        connectedSocket: '2 days 08:15:23',
        
        // Device information
        hwRev: 'AVATTO-01',
        VERSION: 'v0.2.6',
        espModel: 'ESP32-SOLO1',
        deviceTemp: '42.5',
        espCores: '1',
        espFreq: '160',
        espFlashSize: '4',
        espFlashType: 'Internal',
        espHeapFree: '128',
        espHeapSize: '320',
        zigbeeHwRev: 'CC2652P',
        zigbeeFwRev: '20221226',
        
        // Ethernet
        ethConnection: 'Connected',
        ethDhcp: 'Enabled',
        ethIp: '192.168.1.100',
        etchMask: '255.255.255.0',
        ethGate: '192.168.1.1',
        ethSpd: '100 Mbps',
        ethMac: '24:6F:28:12:34:56',
        
        // WiFi
        wifiMode: 'Disabled',
        wifiSsid: 'N/A',
        wifiIp: 'N/A',
        wifiMask: 'N/A',
        wifiGate: 'N/A',
        wifiRssi: 'N/A',
        wifiMac: '24:6F:28:12:34:57',
        
        // VPN
        wgInit: 'Yes',
        wgDeviceAddr: '10.0.0.2',
        wgRemoteAddr: '12.34.56.78:51820'
    },
    
    general: {
        checkedLanMode: 'checked',
        checkedWifiMode: '',
        checkedUsbMode: '',
        keepWeb: 'checked',
        checkedDisableLedPwr: '',
        checkedDisableLedUSB: ''
    },
    
    ethernet: {
        ethDhcp: 'checked',
        ethIp: '192.168.1.100',
        ethMask: '255.255.255.0',
        ethGate: '192.168.1.1',
        ethDns: '8.8.8.8'
    },
    
    wifi: {
        wifiEnabled: 'checked',
        wifiSsid: 'MyNetwork',
        wifiPassword: '********',
        wifiDhcp: 'checked',
        wifiIp: '192.168.1.101',
        wifiMask: '255.255.255.0',
        wifiGate: '192.168.1.1'
    },
    
    serial: {
        '115200': 'selected',  // Serial speed selection
        socketPort: '9999',
        generatedFile: `# Zigbee2MQTT configuration example
serial:
  port: tcp://192.168.1.100:9999
  adapter: auto
  baudrate: 115200

# Optional settings
advanced:
  log_level: info
  network_key: GENERATE
  pan_id: 6754`
    },
    
    security: {
        webAuth: 'checked',
        webUser: 'admin',
        webPass: '********',
        fwEnabled: '',
        fwIp: '192.168.1.50'
    },
    
    mqtt: {
        mqttEnabled: 'checked',
        mqttServer: 'mqtt.example.com',
        mqttPort: '1883',
        mqttUser: 'AVATTO',
        mqttPass: '********',
        mqttTopic: 'zigbee2mqtt',
        mqttInterval: '60',
        mqttDiscovery: 'checked'
    },
    
    wg: {
        wgEnabled: 'checked',
        wgLocalAddr: '10.0.0.2/24',
        wgLocalPrivKey: 'aBcDeFgHiJkLmNoPqRsTuVwXyZ0123456789abcd=',
        wgEndAddr: '12.34.56.78:51820',
        wgEndPubKey: 'ZyXwVuTsRqPoNmLkJiHgFeDcBa9876543210zyxwv='
    },
    
    systools: {
        VERSION: 'v0.2.6',
        latestVersion: 'v0.2.6',
        updateAvailable: false,
        hostname: 'AVATTO-01',
        refreshLogs: '3000',
        timeZones: [
            { value: 'UTC', text: 'UTC +0:00' },
            { value: 'CET-1CEST,M3.5.0,M10.5.0/3', text: 'Europe/Berlin (CET-1CEST) +1:00' },
            { value: 'GMT0BST,M3.5.0/1,M10.5.0', text: 'Europe/London (GMT0BST) +0:00' },
            { value: 'EST5EDT,M3.2.0,M11.1.0', text: 'America/New_York (EST5EDT) -5:00' },
            { value: 'PST8PDT,M3.2.0,M11.1.0', text: 'America/Los_Angeles (PST8PDT) -8:00' },
            { value: 'CST-8', text: 'Asia/Shanghai (CST-8) +8:00' }
        ]
    },
    
    about: {
        VERSION: 'v0.2.6',
        hwRev: 'AVATTO-01',
        espModel: 'ESP32-SOLO1',
        zigbeeHwRev: 'CC2652P'
    }
};

/**
 * 替换页面中的数据占位符
 */
function populateData(page) {
    const data = mockData[page] || {};
    
    // 替换 data-replace 属性的元素
    $('[data-replace]').each(function() {
        const key = $(this).attr('data-replace');
        
        if (data[key] !== undefined) {
            if ($(this).is('input[type="checkbox"]')) {
                $(this).prop('checked', data[key] === 'checked');
            } else if ($(this).is('input[type="radio"]')) {
                $(this).prop('checked', data[key] === 'checked');
            } else if ($(this).is('input')) {
                $(this).val(data[key]);
            } else if ($(this).is('select')) {
                // 处理下拉选择器
                if (key === 'timeZones' && Array.isArray(data[key])) {
                    // 特殊处理时区选择器
                    $(this).empty();
                    data[key].forEach(tz => {
                        $(this).append($('<option></option>')
                            .attr('value', tz.value)
                            .text(tz.text));
                    });
                } else {
                    $(this).val(data[key]);
                }
            } else if ($(this).is('option')) {
                // 处理选项选择状态
                if (data[key] === 'selected') {
                    $(this).prop('selected', true);
                }
            } else if ($(this).is('textarea')) {
                $(this).val(data[key]);
            } else {
                $(this).text(data[key]);
            }
        }
    });
}

/**
 * 用于模拟函数（避免报错）
 */
function checkLatestESPrelease() {
    console.log('Mock: Checking latest ESP release...');
}

function modalConstructor(topic, data) {
    console.log('Mock: Opening modal for:', topic, data);
    
    const modalTitles = {
        'keepWeb': 'Keep Web Server',
        'flashZB': 'Flash Zigbee Module',
        'flashWarning': 'Flash Warning',
        'espFlashGitInfo': 'ESP32 Firmware Information'
    };
    
    const modalContents = {
        'keepWeb': 'When enabled, the web interface and network connection will remain active even in USB mode. This allows you to manage the device remotely.',
        'flashZB': 'This will put the Zigbee module into flash mode. You can then use tools like ZigStar Multi Tool to update the firmware.',
        'flashWarning': '⚠️ Warning: Flashing firmware may take several minutes. Do not power off the device during this process!',
        'espFlashGitInfo': data ? `${data.text}\n\n${data.chglog || ''}` : 'Firmware information loading...'
    };
    
    const title = modalTitles[topic] || 'Information';
    const content = modalContents[topic] || `这是关于 "${topic}" 的信息。\n\n在实际环境中会显示详细的帮助信息。`;
    
    alert(`📋 ${title}\n\n${content}`);
}

function KeepWebDsbl(disabled) {
    console.log('Mock: Keep Web toggle:', disabled);
    $('#keepWeb').prop('disabled', disabled);
}

/**
 * 模拟配置生成器
 */
function generateConfig(type) {
    console.log('Mock: Generating config for:', type);
    
    const configs = {
        z2m: `# Zigbee2MQTT configuration
serial:
  port: tcp://192.168.1.100:9999
  adapter: auto
  baudrate: 115200

advanced:
  log_level: info
  network_key: GENERATE
  pan_id: 6754`,
        
        zha: `# ZHA configuration for Home Assistant
# Add this to your configuration.yaml:

zha:
  zigpy_config:
    ota:
      ikea_provider: true
  device_config:
    !input device_ieee
  database_path: /config/zigbee.db
  serial:
    port: socket://192.168.1.100:9999
    baudrate: 115200`,
        
        usb: `# USB Mode Configuration
# Device will appear as: /dev/ttyUSB0 or COM port
# 
# For Zigbee2MQTT:
serial:
  port: /dev/ttyUSB0
  baudrate: 115200
  
# For ZHA:
# Use the auto-discovery feature or manually specify /dev/ttyUSB0`
    };
    
    $('#generatedFile').val(configs[type] || configs.z2m);
}

/**
 * 复制代码到剪贴板（模拟）
 */
function copyCode() {
    const code = $('#generatedFile').val();
    
    // 尝试使用 Clipboard API
    if (navigator.clipboard) {
        navigator.clipboard.writeText(code).then(() => {
            alert('✅ 配置已复制到剪贴板！');
        }).catch(() => {
            alert('⚠️ 复制失败，请手动选择文本复制');
        });
    } else {
        // 备用方案
        $('#generatedFile').select();
        try {
            document.execCommand('copy');
            alert('✅ 配置已复制到剪贴板！');
        } catch (err) {
            alert('⚠️ 复制失败，请手动选择文本复制');
        }
    }
    
    console.log('Mock: Copied config to clipboard');
}

/**
 * 初始化文件浏览器（模拟）
 */
function initFileBrowser() {
    console.log('Mock: Initializing file browser...');
    
    const mockFiles = [
        { name: '/config/system.json', size: '1.2 KB' },
        { name: '/config/configWifi.json', size: '856 B' },
        { name: '/config/configEther.json', size: '512 B' },
        { name: '/config/configGeneral.json', size: '1.5 KB' },
        { name: '/config/configSecurity.json', size: '432 B' },
        { name: '/config/configSerial.json', size: '256 B' },
        { name: '/config/configMqtt.json', size: '1.1 KB' },
        { name: '/config/configWg.json', size: '892 B' }
    ];
    
    const tbody = $('<tbody></tbody>');
    mockFiles.forEach(file => {
        const row = $('<tr></tr>')
            .css('cursor', 'pointer')
            .hover(
                function() { $(this).css('background-color', '#f8f9fa'); },
                function() { $(this).css('background-color', ''); }
            )
            .click(function() {
                loadMockFile(file.name);
            });
        
        row.append($('<td></td>').text(file.name));
        row.append($('<td></td>').text(file.size));
        tbody.append(row);
    });
    
    $('#filelist tbody').remove();
    $('#filelist').append(tbody);
}

/**
 * 加载模拟文件内容
 */
function loadMockFile(filename) {
    console.log('Mock: Loading file:', filename);
    
    const mockFileContents = {
        '/config/system.json': JSON.stringify({
            hostname: 'AVATTO-01',
            refreshLogs: 3000,
            timezone: 'UTC'
        }, null, 2),
        '/config/configWifi.json': JSON.stringify({
            ssid: 'MyNetwork',
            dhcp: true,
            ip: '192.168.1.101'
        }, null, 2),
        '/config/configEther.json': JSON.stringify({
            dhcp: true,
            ip: '192.168.1.100'
        }, null, 2)
    };
    
    const content = mockFileContents[filename] || `{\n  "mock": "data",\n  "file": "${filename}"\n}`;
    
    $('#filename').val(filename);
    $('#title').text(filename);
    $('#config_file').val(content);
    
    alert(`📄 已加载文件: ${filename}\n\n在实际环境中，您可以编辑并保存此文件。`);
}

/**
 * 初始化调试控制台（模拟）
 */
function initDebugConsole() {
    console.log('Mock: Initializing debug console...');
    
    const mockConsoleData = `[2026-01-19 10:30:15] System started
[2026-01-19 10:30:16] Ethernet connected: 192.168.1.100
[2026-01-19 10:30:17] Zigbee module initialized
[2026-01-19 10:30:18] MQTT connected: mqtt.example.com:1883
[2026-01-19 10:30:19] WebServer started on port 80
[2026-01-19 10:30:20] System ready
`;
    
    $('#console').val(mockConsoleData);
    
    // 模拟实时日志更新
    let logCounter = 0;
    setInterval(() => {
        if ($('#console').length) {
            const now = new Date();
            const timestamp = now.toISOString().replace('T', ' ').substring(0, 19);
            const newLog = `[${timestamp}] Heartbeat #${++logCounter}\n`;
            const currentVal = $('#console').val();
            const lines = currentVal.split('\n');
            
            // 保持最多 20 行
            if (lines.length > 20) {
                lines.shift();
            }
            
            $('#console').val(lines.join('\n') + newLog);
            
            // 自动滚动到底部
            const textarea = document.getElementById('console');
            if (textarea) {
                textarea.scrollTop = textarea.scrollHeight;
            }
        }
    }, 5000);
}

/**
 * 模拟 ESP32 固件更新事件
 */
function ESPfwStartEvents() {
    console.log('Mock: ESP32 firmware update started');
    $('#prg').html('Upload starting...');
    
    let progress = 0;
    const interval = setInterval(() => {
        progress += 10;
        $('#prg').html('Upload progress: ' + progress + '%');
        $('#bar').css('width', progress + '%');
        
        if (progress >= 100) {
            clearInterval(interval);
            $('#prg').html('✅ Update completed! Rebooting...');
            setTimeout(() => {
                alert('ESP32 固件更新完成！\n\n在实际环境中，设备会重启。');
            }, 1000);
        }
    }, 500);
}

/**
 * 模拟 Zigbee 固件更新事件
 */
function ZBfwStartEvents() {
    console.log('Mock: Zigbee firmware update started');
    $('#prg_zb').html('Starting Zigbee flash...');
}

/**
 * 模拟等待固件刷写
 */
function espFlashGitWait() {
    console.log('Mock: Downloading firmware from GitHub...');
    $('#prg').html('Downloading from GitHub...');
    
    let progress = 0;
    const interval = setInterval(() => {
        progress += 5;
        $('#prg').html('Downloading: ' + progress + '%');
        $('#bar').css('width', progress + '%');
        
        if (progress >= 100) {
            clearInterval(interval);
            $('#prg').html('Download complete! Flashing...');
            setTimeout(() => {
                ESPfwStartEvents();
            }, 1000);
        }
    }, 300);
}

/**
 * 处理响应（模拟）
 */
async function processResponses() {
    return {
        textData: '0.2.6',
        jsonData: {
            tag_name: 'v0.2.6',
            body: '## What\'s New\n\n- Bug fixes\n- Performance improvements\n- New features',
            assets: [
                { download_count: 1234 },
                { download_count: 5678 }
            ]
        }
    };
}

/**
 * 模拟 ESP32 文件选择
 */
function sub(obj) {
    let fileName = obj.value.split('\\');
    if (fileName[fileName.length - 1] !== "") {
        $("#updButton").removeAttr("disabled");
        document.getElementById('file-input').innerHTML = '   📄 ' + fileName[fileName.length - 1];
        console.log('Mock: Selected ESP32 file:', fileName[fileName.length - 1]);
    } else {
        $("#updButton").attr("disabled", "disabled");
    }
}

/**
 * 模拟 Zigbee 文件选择
 */
function sub_zb(obj) {
    let fileName = obj.value.split('\\');
    if (fileName[fileName.length - 1] !== "") {
        $("#updButton_zb").removeAttr("disabled");
        document.getElementById('file-input_zb').innerHTML = '   📄 ' + fileName[fileName.length - 1];
        console.log('Mock: Selected Zigbee file:', fileName[fileName.length - 1]);
    } else {
        $("#updButton_zb").attr("disabled", "disabled");
    }
}

/**
 * 模拟重启等待
 */
function rebootWait() {
    console.log('Mock: Device rebooting...');
    alert('🔄 设备正在重启...\n\n在实际环境中，页面会在设备重启后自动刷新。');
}
