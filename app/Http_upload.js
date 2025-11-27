// simple_http_server.js
console.log("=== Simple HTTP Server ===");

// اطلاعات شبکه
var wifiStatus = WiFi.status();
console.log("WiFi Status: " + JSON.stringify(wifiStatus));

if (wifiStatus.ready && wifiStatus.staIP !== "0.0.0.0") {
    console.log("🎯 Your ESP32 IP: " + wifiStatus.staIP);
    console.log("🌐 Open browser and visit:");
    console.log("   http://" + wifiStatus.staIP);
    console.log("   http://" + wifiStatus.staIP + "/apps/");
} else {
    console.log("📡 Connecting to WiFi...");
    WiFi.mode("sta");
    WiFi.autoConnect();
    
    var checkCount = 0;
    var checkInterval = Timer.setInterval(function() {
        var status = WiFi.status();
        if (status.ready && status.staIP !== "0.0.0.0") {
            Timer.clearInterval(checkInterval);
            console.log("✅ Connected! IP: " + status.staIP);
            console.log("🌐 Visit: http://" + status.staIP);
        } else if (checkCount++ > 30) {
            Timer.clearInterval(checkInterval);
            console.log("❌ WiFi connection timeout");
        }
    }, 1000);
}

// نمایش فایل‌های موجود
console.log("📁 Files in /sdcard/apps/:");
try {
    var files = fs.readdirSync("/sdcard/apps");
    for (var i = 0; i < files.length; i++) {
        console.log("   - " + files[i]);
    }
} catch(e) {
    console.log("   (Cannot read directory)");
}

// اطلاعات هر 10 ثانیه
var infoInterval = Timer.setInterval(function() {
    var status = WiFi.status();
    console.log("🔄 Server running - IP: " + status.staIP + " - BACK to stop");
}, 10000);

// توقف
Timer.setInterval(function() {
    if (gpio.readButton(3) === 1) {
        Timer.clearInterval(infoInterval);
        console.log("🛑 Stopping server...");
        lvgl.cleanup_app();
    }
}, 200);