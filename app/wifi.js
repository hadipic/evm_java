// فایل: test_hardware_manager.js
console.log("=== Hardware Manager Test ===");

function testHardwareManager() {
    console.log("1. Testing WiFi initialization...");
    var wifiReady = WiFi.init();
    console.log("   WiFi.init():", wifiReady);
    
    console.log("2. Testing WiFi status...");
    var status = WiFi.status();
    console.log("   Status:", JSON.stringify(status));
    
    console.log("3. Testing WiFi info...");
    var info = WiFi.info();
    console.log("   Info:", JSON.stringify(info));
    
    console.log("4. Testing WiFi mode switching...");
    console.log("   Setting mode to AP...");
    var apResult = WiFi.mode("ap");
    console.log("   AP mode result:", apResult);
    
    Timer.delay(2000);
    
    console.log("   Setting mode to STA for scanning...");
    var staResult = WiFi.mode("sta");
    console.log("   STA mode result:", staResult);
    
    Timer.delay(3000);
    
    console.log("5. Testing WiFi scan in STA mode...");
    console.log("   Starting scan...");
    var scanResult = WiFi.scan();
    console.log("   Scan result type:", typeof scanResult);
    
    if (Array.isArray(scanResult)) {
        console.log("   Found", scanResult.length, "networks");
        for (var i = 0; i < Math.min(scanResult.length, 3); i++) {
            var net = scanResult[i];
            console.log("     " + (i+1) + ". " + net.ssid + " (" + net.rssi + "dBm)");
        }
    } else {
        console.log("   Scan failed or no networks");
    }
    
    console.log("6. Testing mode restoration...");
    console.log("   Setting mode back to AP...");
    var apRestore = WiFi.mode("ap");
    console.log("   AP restore result:", apRestore);
    
    console.log("🎉 Hardware Manager test completed");
}

testHardwareManager();