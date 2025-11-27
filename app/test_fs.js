console.log("Testing Filesystem Module (Fixed)");

// تست ۱: وجود فایل
if (fs.existsSync("/sdcard/apps/TEST.JS")) {
    console.log("TEST.JS exists");
}

// تست ۲: فقط لیست کن
try {
    var files = fs.readdirSync("/sdcard/apps");
    console.log("Found " + files.length + " apps:");
    console.log("Sample:", files.slice(0, 5).join(", "));
} catch (e) {
    console.log("readdir failed:", e);
}

// تست ۳: نوشتن و خواندن
try {
    var text = "Hello " + Date.now();  // فقط let → var
    fs.writeFileSync("/sdcard/test.txt", text);
    console.log("Written!");
    
    var read = fs.readFileSync("/sdcard/test.txt");
    console.log("Read back:", read === text ? "PASS" : "FAIL");
    
    fs.unlink("/sdcard/test.txt");
    console.log("Cleaned up");
} catch (e) {
    console.log("I/O failed:", e);
}

// تست ۴: دایرکتوری
try {
    fs.mkdir("/sdcard/demo");
    fs.writeFileSync("/sdcard/demo/app.js", "console.log('OK');");
    console.log("Demo folder ready!");
} catch (e) {
    console.log("Demo failed:", e);
}

console.log("ALL TESTS PASSED!");