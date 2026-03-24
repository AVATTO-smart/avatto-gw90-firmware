/**
 * 调试功能 - 处理页面切换和交互
 */

// 页面内容映射
const pageContents = {
    root: '../src/websrc/html/PAGE_ROOT.html',
    general: '../src/websrc/html/PAGE_GENERAL.html',
    ethernet: '../src/websrc/html/PAGE_ETHERNET.html',
    wifi: '../src/websrc/html/PAGE_WIFI.html',
    serial: '../src/websrc/html/PAGE_SERIAL.html',
    security: '../src/websrc/html/PAGE_SECURITY.html',
    mqtt: '../src/websrc/html/PAGE_MQTT.html',
    wg: '../src/websrc/html/PAGE_WG.html',
    systools: '../src/websrc/html/PAGE_SYSTOOLS.html',
    about: '../src/websrc/html/PAGE_ABOUT.html'
};

/**
 * 加载页面内容
 */
function loadPage(pageName) {
    const pageUrl = pageContents[pageName];
    
    if (!pageUrl) {
        $('#content-area').html('<div class="alert alert-danger">Page not found</div>');
        return;
    }
    
    // 加载 HTML 内容
    $.get(pageUrl, function(data) {
        $('#content-area').html(data);
        
        // 填充模拟数据
        populateData(pageName);
        
        // 初始化 Masonry（如果页面使用）
        // 注意：对于标签页内的 Masonry，会在标签页切换时单独初始化
        if (pageName !== 'systools' && $('.masonry').length > 0) {
            setTimeout(function() {
                $('.masonry').masonry({
                    percentPosition: true,
                    itemSelector: '.col-sm-12, .col-md-6'
                });
            }, 100);
        }
        
        // 添加表单提交拦截（避免实际提交）
        $('form').on('submit', function(e) {
            e.preventDefault();
            const formData = $(this).serializeArray();
            console.log('Form submitted (mock):', formData);
            alert('表单数据已记录到控制台\n\n在实际环境中，这些数据会被保存到设备。');
        });
        
        // 添加按钮点击拦截
        $('button[data-cmd]').on('click', function() {
            const cmd = $(this).attr('data-cmd');
            console.log('Command triggered:', cmd);
            alert('命令 ' + cmd + ' 已触发\n\n在实际环境中，这会控制设备的 LED 或其他功能。');
        });
        
        // 初始化配置生成器（Serial 页面）
        if (pageName === 'serial') {
            setTimeout(() => {
                if (typeof generateConfig === 'function') {
                    generateConfig('z2m'); // 默认生成 Z2M 配置
                }
            }, 200);
        }
        
        // 初始化 System Tools 标签页功能
        if (pageName === 'systools') {
            setTimeout(() => {
                // 确保 Bootstrap 标签页功能正常工作
                initSystemToolsTabs();
            }, 200);
        }
        
        console.log('Loaded page:', pageName);
    }).fail(function() {
        $('#content-area').html('<div class="alert alert-danger">Failed to load page content</div>');
    });
}

/**
 * 更新导航活动状态
 */
function updateNavigation(pageName) {
    $('.nav-link').removeClass('active');
    $('.nav-link[data-page="' + pageName + '"]').addClass('active');
    $('#pageSelect').val(pageName);
    
    // 更新页面标题
    const pageTitles = {
        root: 'Dashboard',
        general: 'General Settings',
        ethernet: 'Ethernet Configuration',
        wifi: 'WiFi Configuration',
        serial: 'Serial / Z2M and ZHA',
        security: 'Security Settings',
        mqtt: 'MQTT Configuration',
        wg: 'WireGuard VPN',
        systools: 'System and Tools',
        about: 'About'
    };
    
    $('#pagenamePC').text(pageTitles[pageName] || 'AVATTO-GW90-Ti');
}

/**
 * 初始化
 */
$(document).ready(function() {
    console.log('Debug UI initialized');
    
    // 默认加载 Dashboard
    loadPage('root');
    
    // 下拉选择器切换页面
    $('#pageSelect').on('change', function() {
        const pageName = $(this).val();
        loadPage(pageName);
        updateNavigation(pageName);
    });
    
    // 侧边栏导航点击
    $('.nav-link').on('click', function(e) {
        e.preventDefault();
        const pageName = $(this).attr('data-page');
        loadPage(pageName);
        updateNavigation(pageName);
    });
    
    // 添加一些调试快捷键
    $(document).on('keydown', function(e) {
        // Ctrl + R: 重新加载当前页面
        if (e.ctrlKey && e.key === 'r') {
            e.preventDefault();
            const currentPage = $('#pageSelect').val();
            console.log('Reloading page:', currentPage);
            loadPage(currentPage);
        }
        
        // Ctrl + D: 打开浏览器开发者工具提示
        if (e.ctrlKey && e.key === 'd') {
            e.preventDefault();
            console.log('=== Debug Info ===');
            console.log('Current page:', $('#pageSelect').val());
            console.log('Mock data:', mockData);
            console.log('==================');
        }
    });
    
    console.log('提示：');
    console.log('- 使用顶部的下拉菜单或侧边栏切换页面');
    console.log('- 按 Ctrl+R 重新加载当前页面');
    console.log('- 按 Ctrl+D 查看调试信息');
    console.log('- 修改 mock-data.js 来改变显示的数据');
    console.log('- 在 index.html 的 <style> 标签中测试新样式');
});

/**
 * 初始化 System Tools 页面的标签页功能
 */
function initSystemToolsTabs() {
    console.log('Initializing System Tools tabs...');
    
    // 确保标签页按钮可以切换
    const tabButtons = document.querySelectorAll('[data-bs-toggle="tab"]');
    tabButtons.forEach(button => {
        button.addEventListener('click', function(e) {
            e.preventDefault();
            
            // 移除所有活动状态
            tabButtons.forEach(btn => {
                btn.classList.remove('active');
                btn.setAttribute('aria-selected', 'false');
            });
            
            document.querySelectorAll('.tab-pane').forEach(pane => {
                pane.classList.remove('show', 'active');
            });
            
            // 激活当前标签
            this.classList.add('active');
            this.setAttribute('aria-selected', 'true');
            
            // 显示对应内容
            const target = this.getAttribute('data-bs-target');
            const targetPane = document.querySelector(target);
            if (targetPane) {
                targetPane.classList.add('show', 'active');
                
                // 重新初始化 Masonry 布局（延迟以确保内容已显示）
                setTimeout(() => {
                    const $masonry = $(targetPane).find('.masonry');
                    if ($masonry.length > 0) {
                        console.log('Re-initializing Masonry for:', target);
                        
                        // 销毁现有的 Masonry 实例
                        if ($masonry.data('masonry')) {
                            $masonry.masonry('destroy');
                        }
                        
                        // 重新创建 Masonry 实例
                        $masonry.masonry({
                            percentPosition: true,
                            itemSelector: '.col-sm-12, .col-md-6'
                        });
                        
                        // 强制重新布局
                        $masonry.masonry('layout');
                    }
                }, 150);
            }
            
            console.log('Switched to tab:', target);
        });
    });
    
    // 初始化第一个标签页的 Masonry
    setTimeout(() => {
        const activePane = document.querySelector('.tab-pane.active');
        if (activePane) {
            const $masonry = $(activePane).find('.masonry');
            if ($masonry.length > 0) {
                $masonry.masonry({
                    percentPosition: true,
                    itemSelector: '.col-sm-12, .col-md-6'
                });
            }
        }
    }, 200);
    
    // 初始化文件浏览器数据
    initFileBrowser();
    
    // 初始化控制台
    initDebugConsole();
}
