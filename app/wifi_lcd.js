console.log("=== WiFi LCD Manager START ===");

// ==================== تنظیمات اولیه ====================
var MODES = {
    SCAN: 0,
    INFO: 1,
    ABOUT: 2
};

var currentMode = MODES.SCAN;
var scanCount = 0;
var networks = [];
var appRunning = true;
var lastScanTime = 0;
var lastAutoScanTime = 0;

// ==================== راه‌اندازی WiFi ====================
console.log("📡 Initializing WiFi...");
try {
    WiFi.init();
    console.log("✅ WiFi initialized");
} catch (e) {
    console.log("❌ WiFi init failed:", e);
}

// ==================== ایجاد رابط کاربری LCD ====================
console.log("🖥️ Creating LCD interface...");

// صفحه اصلی
var screen = lvgl.scr_act();
lvgl.obj_set_style_local_bg_color(screen, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_BLACK);

// عنوان برنامه
var titleLabel = lvgl.label_create(screen);
lvgl.obj_set_size(titleLabel, 150, 20);
lvgl.obj_set_pos(titleLabel, 5, 5);
lvgl.obj_set_style_local_text_color(titleLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_CYAN);

// وضعیت
var statusLabel = lvgl.label_create(screen);
lvgl.obj_set_size(statusLabel, 150, 40);
lvgl.obj_set_pos(statusLabel, 5, 25);
lvgl.obj_set_style_local_text_color(statusLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_WHITE);

// کانتینر برای لیست شبکه‌ها
var listContainer = lvgl.obj_create(screen);
lvgl.obj_set_size(listContainer, 150, 70);
lvgl.obj_set_pos(listContainer, 5, 65);
lvgl.obj_set_style_local_bg_color(listContainer, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.color_hex(0x222222));
lvgl.obj_set_style_local_radius(listContainer, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, 5);

// لیبل‌های شبکه
var networkLabels = [];
for (var i = 0; i < 3; i++) {
    var label = lvgl.label_create(listContainer);
    lvgl.obj_set_size(label, 140, 20);
    lvgl.obj_set_pos(label, 5, 5 + i * 22);
    lvgl.obj_set_style_local_text_color(label, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_WHITE);
    networkLabels.push(label);
}

// اطلاعات پایین
var infoLabel = lvgl.label_create(screen);
lvgl.obj_set_size(infoLabel, 150, 20);
lvgl.obj_set_pos(infoLabel, 5, 140);
lvgl.obj_set_style_local_text_color(infoLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_YELLOW);

// راهنما
var helpLabel = lvgl.label_create(screen);
lvgl.obj_set_size(helpLabel, 150, 20);
lvgl.obj_set_pos(helpLabel, 5, 160);
lvgl.obj_set_style_local_text_color(helpLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_GREEN);

console.log("✅ LCD interface created");

// ==================== توابع کمکی ====================
function getSignalBars(rssi) {
    if (rssi > -55) return "***";
    if (rssi > -65) return "**";
    if (rssi > -75) return "*";
    return ".";
}

function formatSSID(ssid, maxLength) {
    if (!ssid) return "Unknown";
    if (ssid.length <= maxLength) return ssid;
    return ssid.substring(0, maxLength - 2) + "..";
}

function getAuthString(auth) {
    if (!auth) return "Open";
    if (auth.indexOf("WPA2") >= 0) return "WPA2";
    if (auth.indexOf("WPA") >= 0) return "WPA";
    if (auth.indexOf("WEP") >= 0) return "WEP";
    return auth;
}

// ==================== توابع به‌روزرسانی UI ====================
function updateTitle() {
    var modeNames = ["SCAN", "INFO", "ABOUT"];
    var modeColors = [lvgl.COLOR_CYAN, lvgl.COLOR_GREEN, lvgl.COLOR_YELLOW];
    
    lvgl.label_set_text(titleLabel, "WiFi Manager - " + modeNames[currentMode]);
    lvgl.obj_set_style_local_text_color(titleLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, modeColors[currentMode]);
}

function updateStatus() {
    try {
        var wifiStatus = WiFi.status();
        var statusText = "Mode: " + (wifiStatus.ap ? "AP" : "STA") + "\n";
        statusText += "Scans: " + scanCount + "\n";
        statusText += "Networks: " + networks.length;
        
        lvgl.label_set_text(statusLabel, statusText);
    } catch (e) {
        lvgl.label_set_text(statusLabel, "Mode: Scanner\nScans: " + scanCount + "\nNetworks: " + networks.length);
    }
}

function updateNetworkList() {
    // پاک کردن لیست
    for (var i = 0; i < networkLabels.length; i++) {
        lvgl.label_set_text(networkLabels[i], "");
        lvgl.obj_set_style_local_text_color(networkLabels[i], lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_WHITE);
    }
    
    if (networks.length === 0) {
        lvgl.label_set_text(networkLabels[0], "No networks");
        lvgl.obj_set_style_local_text_color(networkLabels[0], lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_GRAY);
        return;
    }
    
    // نمایش شبکه‌ها
    for (var i = 0; i < Math.min(networks.length, networkLabels.length); i++) {
        var net = networks[i];
        var signalBars = getSignalBars(net.rssi);
        var text = signalBars + " " + formatSSID(net.ssid, 12);
        
        lvgl.label_set_text(networkLabels[i], text);
        
        // رنگ بر اساس قدرت سیگنال
        if (net.rssi > -60) {
            lvgl.obj_set_style_local_text_color(networkLabels[i], lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_GREEN);
        } else if (net.rssi > -70) {
            lvgl.obj_set_style_local_text_color(networkLabels[i], lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_YELLOW);
        } else {
            lvgl.obj_set_style_local_text_color(networkLabels[i], lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_RED);
        }
    }
}

function updateInfo() {
    var infoText = "";
    
    switch(currentMode) {
        case MODES.SCAN:
            infoText = "Networks: " + networks.length;
            if (networks.length > 0) {
                var bestNet = networks[0];
                infoText += " | Best: " + bestNet.rssi + "dBm";
            }
            break;
            
        case MODES.INFO:
            try {
                var wifiInfo = WiFi.info();
                var wifiStatus = WiFi.status();
                infoText = "AP: " + wifiStatus.apIP;
                if (wifiInfo.apSSID) {
                    infoText += " | " + wifiInfo.apSSID;
                }
            } catch (e) {
                infoText = "WiFi Info: Error";
            }
            break;
            
        case MODES.ABOUT:
            infoText = "WiFi Scanner v2.0 | Real Scan";
            break;
    }
    
    lvgl.label_set_text(infoLabel, infoText);
}

function updateHelp() {
    var helpText = "";
    
    switch(currentMode) {
        case MODES.SCAN:
            helpText = "▲/▼: Mode  SELECT: Scan  BACK: Exit";
            break;
            
        case MODES.INFO:
            helpText = "▲/▼: Mode  SELECT: Refresh  BACK: Exit";
            break;
            
        case MODES.ABOUT:
            helpText = "▲/▼: Mode  SELECT: Back  BACK: Exit";
            break;
    }
    
    lvgl.label_set_text(helpLabel, helpText);
}

// ==================== توابع عملیاتی ====================
function performScan() {
    console.log("🔍 Scanning for WiFi networks...");
    scanCount++;
    
    try {
        // تغییر به حالت STA برای اسکن بهتر
        var currentStatus = WiFi.status();
        if (currentStatus.ap && !currentStatus.sta) {
            console.log("🔄 Switching to STA mode for better scanning...");
            WiFi.mode("sta");
            Timer.delay(3000); // تأخیر برای تثبیت حالت
        }
        
        var scanResult = WiFi.scan();
        
        if (scanResult && scanResult.length > 0) {
            networks = scanResult.sort(function(a, b) {
                return b.rssi - a.rssi;
            });
            
            console.log("✅ Found " + networks.length + " networks");
            
            // نمایش جزئیات شبکه‌ها در کنسول
            for (var i = 0; i < Math.min(networks.length, 5); i++) {
                var net = networks[i];
                console.log("  " + (i+1) + ". " + net.ssid + " (" + net.rssi + "dBm) - " + getAuthString(net.auth));
            }
            
        } else {
            networks = [];
            console.log("❌ No networks found");
        }
        
        // بازگشت به حالت AP
        if (currentStatus.ap && !currentStatus.sta) {
            Timer.delay(1000);
            WiFi.mode("ap");
            console.log("🔄 Restored AP mode");
        }
        
    } catch (e) {
        console.log("❌ Scan error:", e);
        networks = [];
    }
    
    updateNetworkList();
    updateInfo();
    updateStatus();
    lastScanTime = Timer.getTime();
    lastAutoScanTime = Timer.getTime();
}

function handleButtonPress(button) {
    console.log("🎮 Button: " + button);
    
    switch(button) {
        case "UP":
            currentMode = (currentMode - 1 + 3) % 3;
            updateAllUI();
            Timer.delay(200);
            break;
            
        case "DOWN":
            currentMode = (currentMode + 1) % 3;
            updateAllUI();
            Timer.delay(200);
            break;
            
        case "SELECT":
            if (currentMode === MODES.SCAN) {
                performScan();
            } else if (currentMode === MODES.INFO) {
                updateStatus();
                console.log("ℹ️ Status refreshed");
            } else if (currentMode === MODES.ABOUT) {
                currentMode = MODES.SCAN;
                updateAllUI();
            }
            Timer.delay(300);
            break;
            
        case "BACK":
            console.log("⏹️ Exit requested");
            appRunning = false;
            break;
    }
}

function updateAllUI() {
    updateTitle();
    updateStatus();
    updateNetworkList();
    updateInfo();
    updateHelp();
}

function autoScanIfNeeded() {
    var currentTime = Timer.getTime();
    
    // اسکن خودکار هر 60 ثانیه در حالت SCAN
    if (currentMode === MODES.SCAN && currentTime - lastAutoScanTime > 60000) {
        console.log("🔄 Auto-scanning...");
        performScan();
    }
}

// ==================== راه‌اندازی اولیه ====================
function initializeApp() {
    console.log("🚀 Starting WiFi LCD Manager...");
    
    updateAllUI();
    lastScanTime = Timer.getTime();
    lastAutoScanTime = Timer.getTime();
    
    // اسکن اولیه بعد از 3 ثانیه
    console.log("⏳ Initial scan in 3 seconds...");
    for (var i = 0; i < 30; i++) {
        Timer.delay(100);
    }
    performScan();
    
    console.log("✅ WiFi LCD Manager Ready!");
    console.log("📡 Mode: Real WiFi Scanner");
    console.log("🎮 Controls: UP/DOWN - Change mode, SELECT - Action, BACK - Exit");
}

// ==================== حلقه اصلی برنامه ====================
function mainLoop() {
    console.log("🔄 Starting main loop...");
    
    var loopCounter = 0;
    
    while (appRunning) {
        loopCounter++;
        
        // شبیه‌سازی دکمه برای تست - هر 100 حلقه یک دکمه تصادفی
        var upPressed = false;
        var selectPressed = false;
        var downPressed = false;
        var backPressed = false;
        
        if (loopCounter % 100 === 0) {
            var randomButton = Math.floor(Math.random() * 4);
            switch(randomButton) {
                case 0: upPressed = true; break;
                case 1: selectPressed = true; break;
                case 2: downPressed = true; break;
                case 3: backPressed = true; break;
            }
            
            console.log("🎮 Simulated button: " + 
                (upPressed ? "UP" : selectPressed ? "SELECT" : downPressed ? "DOWN" : "BACK"));
        }
        
        if (upPressed) handleButtonPress("UP");
        if (downPressed) handleButtonPress("DOWN");
        if (selectPressed) handleButtonPress("SELECT");
        if (backPressed) handleButtonPress("BACK");
        
        // آپدیت خودکار
        autoScanIfNeeded();
        
        Timer.delay(100);
    }
    
    cleanup();
}

function cleanup() {
    console.log("🧹 Cleaning up...");
    
    // پاک‌سازی منابع LVGL
    lvgl.obj_del(listContainer);
    lvgl.obj_del(titleLabel);
    lvgl.obj_del(statusLabel);
    lvgl.obj_del(infoLabel);
    lvgl.obj_del(helpLabel);
    
    console.log("✅ Cleanup completed");
}

// ==================== شروع برنامه ====================
initializeApp();
mainLoop();

console.log("=== WiFi LCD Manager FINISHED ===");