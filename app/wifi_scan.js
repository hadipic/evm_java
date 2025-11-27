console.log("=== SAFE WiFi LCD Manager ===");

// ==================== تنظیمات اولیه ====================
var MODES = {
    SCAN: 0,
    INFO: 1,
    SETTINGS: 2
};

var currentMode = MODES.SCAN;
var scanCount = 0;
var networks = [];
var appRunning = true;
var lastScanTime = 0;

// ==================== راه‌اندازی ایمن WiFi ====================
console.log("🔒 Starting in SAFE mode (read-only WiFi)");

// فقط WiFi را init کنیم اما AP ایجاد نکنیم
try {
    var wifiReady = WiFi.init();
    console.log("✅ WiFi.init():", wifiReady);
} catch (e) {
    console.log("❌ WiFi.init() failed:", e);
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

// لیست شبکه‌ها
var listContainer = lvgl.obj_create(screen);
lvgl.obj_set_size(listContainer, 150, 70);
lvgl.obj_set_pos(listContainer, 5, 65);
lvgl.obj_set_style_local_bg_color(listContainer, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.color_hex(0x222222));

var networkLabels = [];
for (var i = 0; i < 3; i++) {
    var label = lvgl.label_create(listContainer);
    lvgl.obj_set_size(label, 140, 20);
    lvgl.obj_set_pos(label, 5, 5 + i * 22);
    lvgl.obj_set_style_local_text_color(label, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_WHITE);
    networkLabels.push(label);
}

// اطلاعات
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

// ==================== توابع به‌روزرسانی UI ====================
function updateTitle() {
    var modeNames = ["SCAN", "INFO", "ABOUT"];
    var modeColors = [lvgl.COLOR_CYAN, lvgl.COLOR_GREEN, lvgl.COLOR_YELLOW];
    
    lvgl.label_set_text(titleLabel, "WiFi Scanner - " + modeNames[currentMode]);
    lvgl.obj_set_style_local_text_color(titleLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, modeColors[currentMode]);
}

function updateStatus() {
    try {
        var status = WiFi.status();
        var infoText = "Mode: READ-ONLY\n";
        infoText += "Networks: " + networks.length + "\n";
        infoText += "Scans: " + scanCount;
        
        lvgl.label_set_text(statusLabel, infoText);
    } catch (e) {
        lvgl.label_set_text(statusLabel, "Mode: READ-ONLY\nStatus: Ready\nScans: " + scanCount);
    }
}

function updateNetworkList() {
    if (networks.length === 0) {
        lvgl.label_set_text(networkLabels[0], "No networks");
        for (var i = 1; i < networkLabels.length; i++) {
            lvgl.label_set_text(networkLabels[i], "");
        }
        return;
    }
    
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
            infoText = "Scans: " + scanCount + " | Found: " + networks.length;
            if (networks.length > 0) {
                infoText += " | Best: " + networks[0].rssi + "dBm";
            }
            break;
            
        case MODES.INFO:
            try {
                var status = WiFi.status();
                infoText = "WiFi Status: " + (status ? "OK" : "Error");
            } catch (e) {
                infoText = "WiFi Status: Error";
            }
            break;
            
        case MODES.SETTINGS:
            infoText = "Safe WiFi Scanner v1.0";
            break;
    }
    
    lvgl.label_set_text(infoLabel, infoText);
}

function updateHelp() {
    var helpText = "";
    
    switch(currentMode) {
        case MODES.SCAN:
            helpText = "UP/DOWN: Mode  SELECT: Scan";
            break;
            
        case MODES.INFO:
            helpText = "UP/DOWN: Mode  SELECT: Refresh";
            break;
            
        case MODES.SETTINGS:
            helpText = "UP/DOWN: Mode  SELECT: Back";
            break;
    }
    
    lvgl.label_set_text(helpLabel, helpText);
}

// ==================== توابع عملیاتی ====================
function performScan() {
    console.log("🔍 Scanning WiFi networks...");
    scanCount++;
    
    try {
        var scanResult = WiFi.scan();
        
        if (scanResult && scanResult.length > 0) {
            networks = scanResult.sort(function(a, b) {
                return b.rssi - a.rssi;
            });
            console.log("✅ Found " + networks.length + " networks");
            
            // نمایش قوی‌ترین شبکه
            if (networks.length > 0) {
                var best = networks[0];
                console.log("📶 Best: " + best.ssid + " (" + best.rssi + "dBm)");
            }
        } else {
            networks = [];
            console.log("❌ No networks found");
        }
        
    } catch (e) {
        console.log("❌ Scan error:", e);
        networks = [];
    }
    
    updateNetworkList();
    updateInfo();
    updateStatus();
    lastScanTime = Timer.getTime();
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
    
    // اسکن خودکار هر 30 ثانیه در حالت SCAN
    if (currentMode === MODES.SCAN && currentTime - lastScanTime > 30000) {
        performScan();
    }
}

// ==================== راه‌اندازی اولیه ====================
function initializeApp() {
    console.log("🚀 Starting Safe WiFi Scanner...");
    
    updateAllUI();
    lastScanTime = Timer.getTime();
    
    // اسکن اولیه بعد از 2 ثانیه
    console.log("⏳ Initial scan in 2 seconds...");
    for (var i = 0; i < 20; i++) {
        Timer.delay(100);
    }
    performScan();
    
    console.log("✅ Safe WiFi Scanner Ready!");
    console.log("📡 Mode: Read-only (Scan only)");
    console.log("🎮 Use buttons to navigate and scan");
}

// ==================== حلقه اصلی برنامه ====================
function mainLoop() {
    console.log("🔄 Starting main loop...");
    
    var loopCounter = 0;
    
    while (appRunning) {
        loopCounter++;
        
        // شبیه‌سازی دکمه برای تست - هر 50 حلقه یک دکمه
        var upPressed = false;
        var selectPressed = false;
        var downPressed = false;
        var backPressed = false;
        
        if (loopCounter % 50 === 0) {
            var randomButton = Math.floor(Math.random() * 4);
            switch(randomButton) {
                case 0: upPressed = true; break;
                case 1: selectPressed = true; break;
                case 2: downPressed = true; break;
                case 3: backPressed = true; break;
            }
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

console.log("=== Safe WiFi LCD Manager FINISHED ===");