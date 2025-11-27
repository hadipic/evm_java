// ftp_upload_fixed.js
console.log("=== FTP Upload Fixed ===");

var appRunning = true;

// ابتدا WiFi رو به حالت STA تغییر بده
console.log("1. Setting WiFi to STA mode...");
WiFi.mode("sta");

// اتصال به WiFi
console.log("2. Connecting to WiFi...");
var connectResult = WiFi.sta("Shop-electronic", "Bashniji@#1401"); // جایگزین کن با SSID و پسورد واقعی
console.log("WiFi.sta result: " + connectResult);

// صبر کن تا WiFi وصل بشه
var waitCount = 0;
var waitInterval = Timer.setInterval(function() {
    var status = WiFi.status();
    console.log("WiFi status - IP: " + status.staIP + ", Ready: " + status.ready);
    
    if (status.ready && status.staIP !== "0.0.0.0") {
        Timer.clearInterval(waitInterval);
        console.log("✅ WiFi connected! IP: " + status.staIP);
        startFTPServer();
    } else if (waitCount++ > 30) {
        Timer.clearInterval(waitInterval);
        console.log("❌ WiFi connection timeout");
        console.log("Current status: " + JSON.stringify(status));
        
        // حتی اگر وصل نشد، FTP رو تست کن
        testFTPWithoutWiFi();
    }
}, 2000);

function startFTPServer() {
    console.log("3. Starting FTP server...");
    
    // شروع FTP سرور
    if (FTP.start(2121)) {
        console.log("✅ FTP server started on port 2121");
        console.log("🎯 Your FTP Server is ready!");
        console.log("📡 Address: ftp://" + WiFi.status().staIP + ":2121");
        console.log("📁 Root directory: /sdcard");
        console.log("👤 Use any username/password to login");
        
        // نمایش وضعیت
        Timer.setInterval(function() {
            var ftpStatus = FTP.status();
            var wifiStatus = WiFi.status();
            console.log("🔄 FTP: " + (ftpStatus.running ? "RUNNING" : "STOPPED") + 
                       " | WiFi: " + wifiStatus.staIP);
        }, 10000);
        
    } else {
        console.log("❌ Failed to start FTP server");
    }
}

function testFTPWithoutWiFi() {
    console.log("🔧 Testing FTP without WiFi...");
    
    var status = FTP.status();
    console.log("FTP Status: " + JSON.stringify(status));
    
    console.log("Trying to start FTP...");
    var result = FTP.start(2121);
    console.log("FTP.start result: " + result);
    
    status = FTP.status();
    console.log("FTP Status after start: " + JSON.stringify(status));
    
    if (status.running) {
        console.log("🎉 FTP server is running (but no WiFi)");
    }
}

// توقف با دکمه BACK
Timer.setInterval(function() {
    if (gpio.readButton(3) === 1) {
        appRunning = false;
        console.log("🛑 Stopping FTP server...");
        FTP.stop();
        Timer.delay(500);
        lvgl.cleanup_app();
    }
}, 200);