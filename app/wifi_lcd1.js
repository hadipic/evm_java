console.log("=== Fixed WiFi Manager START ===");

// ==================== تنظیمات ====================
var MODES = {
    SCAN: 0,
    INFO: 1,
    ABOUT: 2,
    CONNECT: 3,
    FILTER: 4
};
var currentMode = MODES.SCAN;
var scanCount = 0;
var networks = [];
var favorites = [];
var selectedNetIndex = 0;
var appRunning = true;
var lastScanTime = 0;
var lastAutoScanTime = 0;
var filterMode = 'all';
var passwordInput = '';

// ==================== UI ====================
console.log("🖥️ Creating UI...");
var screen = lvgl.scr_act();
lvgl.obj_set_style_local_bg_color(screen, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_BLACK);

var titleLabel = lvgl.label_create(screen);
lvgl.obj_set_size(titleLabel, 150, 20);
lvgl.obj_set_pos(titleLabel, 0, 0);
lvgl.obj_set_style_local_text_color(titleLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_CYAN);

var statusLabel = lvgl.label_create(screen);
lvgl.obj_set_size(statusLabel, 150, 30);
lvgl.obj_set_pos(statusLabel, 0, 20);
lvgl.obj_set_style_local_text_color(statusLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_WHITE);

var networkLabels = [];
for (var i = 0; i < 3; i++) {
    var label = lvgl.label_create(screen);
    lvgl.obj_set_size(label, 150, 20);
    lvgl.obj_set_pos(label, 0, 50 + i * 20);
    lvgl.obj_set_style_local_text_color(label, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_WHITE);
    networkLabels.push(label);
}

var infoLabel = lvgl.label_create(screen);
lvgl.obj_set_size(infoLabel, 150, 20);
lvgl.obj_set_pos(infoLabel, 0, 110);
lvgl.obj_set_style_local_text_color(infoLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_YELLOW);

var helpLabel = lvgl.label_create(screen);
lvgl.obj_set_size(helpLabel, 150, 20);
lvgl.obj_set_pos(helpLabel, 0, 130);
lvgl.obj_set_style_local_text_color(helpLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_GREEN);

console.log("✅ UI ready");

// ==================== کمکی ====================
function getSignalBars(rssi) {
    if (rssi > -55) return "***";
    if (rssi > -65) return "**";
    if (rssi > -75) return "*";
    return ".";
}

function formatSSID(ssid, maxLen) {
    if (!ssid) return "Hidden";
    if (ssid.length <= maxLen) return ssid;
    return ssid.substring(0, maxLen - 3) + "...";
}

function getAuthString(auth) {
    if (!auth || auth === "OPEN") return "Open";
    if (auth.indexOf("WPA2") >= 0) return "WPA2";
    if (auth.indexOf("WPA") >= 0) return "WPA";
    if (auth.indexOf("WEP") >= 0) return "WEP";
    return auth;
}

function filterNetworks() {
    var result = [];
    for (var i = 0; i < networks.length; i++) {
        var net = networks[i];
        var match = false;
        if (filterMode === 'all') match = true;
        else if (filterMode === 'wpa2' && net.auth && net.auth.indexOf('WPA2') >= 0) match = true;
        else if (filterMode === 'open' && (!net.auth || net.auth === "OPEN")) match = true;
        if (match) result.push(net);
    }
    return result;
}

function isFavorite(ssid) {
    for (var i = 0; i < favorites.length; i++) {
        if (favorites[i].ssid === ssid) return true;
    }
    return false;
}

function toggleFavorite(ssid, rssi, auth) {
    for (var i = 0; i < favorites.length; i++) {
        if (favorites[i].ssid === ssid) {
            favorites.splice(i, 1);
            console.log("⭐ Removed: " + ssid);
            return;
        }
    }
    favorites.push({ssid: ssid, rssi: rssi, auth: auth});
    if (favorites.length > 5) favorites.shift();
    console.log("⭐ Added: " + ssid);
}

// ==================== UI Update ====================
function updateTitle() {
    var modeNames = ["SCAN", "INFO", "ABOUT", "CONNECT", "FILTER"];
    var colors = [lvgl.COLOR_CYAN, lvgl.COLOR_GREEN, lvgl.COLOR_YELLOW, lvgl.COLOR_MAGENTA, lvgl.COLOR_ORANGE];
    lvgl.label_set_text(titleLabel, "WiFi Mgr - " + modeNames[currentMode]);
    lvgl.obj_set_style_local_text_color(titleLabel, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, colors[currentMode % colors.length]);
}

function updateStatus() {
    try {
        var status = WiFi.status();
        var text = "Mode: " + (status.ap ? "AP" : "STA") + " | Scans: " + scanCount + " | Filter: " + filterMode;
        lvgl.label_set_text(statusLabel, text);
    } catch (e) {
        lvgl.label_set_text(statusLabel, "Mode: STA | Scans: " + scanCount);
    }
}

function updateNetworkList() {
    var filtered = filterNetworks();
    for (var i = 0; i < networkLabels.length; i++) {
        lvgl.label_set_text(networkLabels[i], "");
    }
    if (filtered.length === 0) {
        lvgl.label_set_text(networkLabels[0], "No networks");
        return;
    }
    for (var i = 0; i < Math.min(filtered.length, 3); i++) {
        var net = filtered[i];
        var bars = getSignalBars(net.rssi);
        var text = (i === selectedNetIndex ? "> " : "  ") + bars + " " + formatSSID(net.ssid, 12) + " " + getAuthString(net.auth);
        if (isFavorite(net.ssid)) text += " ⭐";
        lvgl.label_set_text(networkLabels[i], text);
        var color = net.rssi > -60 ? lvgl.COLOR_GREEN : (net.rssi > -70 ? lvgl.COLOR_YELLOW : lvgl.COLOR_RED);
        lvgl.obj_set_style_local_text_color(networkLabels[i], lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, color);
    }
}

function updateInfo() {
    var filtered = filterNetworks();
    var text = "Nets: " + filtered.length + "/" + networks.length;
    if (filtered.length > 0) {
        var best = filtered[0];
        text += " | Best: " + best.rssi + "dBm";
    }
    lvgl.label_set_text(infoLabel, text);
}

function updateHelp() {
    var text = "UP/DN: Nav | SELECT: Act | BACK: Mode";
    lvgl.label_set_text(helpLabel, text);
}

function updateAllUI() {
    updateTitle();
    updateStatus();
    updateNetworkList();
    updateInfo();
    updateHelp();
}

// ==================== عملیات ====================
function performScan() {
    console.log("🔍 Scanning...");
    scanCount++;
    try {
        var result = WiFi.scan();
        if (result && result.length > 0) {
            networks = result.sort(function(a, b) { return b.rssi - a.rssi; });
            console.log("✅ " + networks.length + " nets");
        } else {
            networks = [];
        }
    } catch (e) {
        console.log("❌ Scan err:", e);
    }
    lastScanTime = Timer.getTime();
    updateAllUI();
}

function connectToNetwork(ssid, pass) {
    try {
        console.log("🔗 Conn to " + ssid);
        WiFi.sta(ssid, pass);
        setTimeout(function() {
            console.log("Conn: " + (WiFi.isStaConnected() ? "OK" : "Fail"));
        }, 5000);
    } catch (e) {
        console.log("❌ Conn err:", e);
    }
    currentMode = MODES.SCAN;
    updateAllUI();
}

function handleButton(button, isLong) {
    console.log("🎮 " + button);
    var filtered = filterNetworks();
    switch (button) {
        case 0:  // UP
            if (currentMode === MODES.SCAN) selectedNetIndex = Math.max(0, selectedNetIndex - 1);
            else currentMode = (currentMode - 1 + 5) % 5;
            break;
        case 2:  // DOWN
            if (currentMode === MODES.SCAN) selectedNetIndex = Math.min(filtered.length - 1, selectedNetIndex + 1);
            else currentMode = (currentMode + 1) % 5;
            break;
        case 1:  // SELECT
            if (currentMode === MODES.SCAN) performScan();
            else if (currentMode === MODES.CONNECT) {
                var net = networks[selectedNetIndex];
                if (net) connectToNetwork(net.ssid, passwordInput);
            }
            else currentMode = MODES.SCAN;
            break;
        case 3:  // BACK
            appRunning = false;
            break;
    }
    if (isLong && currentMode === MODES.SCAN) {
        var net = filtered[selectedNetIndex];
        if (net) toggleFavorite(net.ssid, net.rssi, net.auth);
    }
    updateAllUI();
    Timer.delay(200);
}

// ==================== اصلی ====================
function mainLoop() {
    console.log("🔄 Loop start");
    updateAllUI();
    Timer.delay(2000);
    performScan();

    var debounce = 0;
    while (appRunning) {
        for (var btn = 0; btn < 4; btn++) {
            var pressed = gpio.readButton(btn);
            if (pressed === 1 && Timer.getTime() - debounce > 300) {
                handleButton(btn, Timer.getTime() - debounce > 1000);
                debounce = Timer.getTime();
            }
        }
        Timer.delay(50);
    }
    lvgl.cleanup_app();
}

mainLoop();
console.log("=== Fixed WiFi END ===");