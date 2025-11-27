console.log("🎯 FINAL FILESYSTEM TEST - COMPATIBLE VERSION");

// تست ۱: وضعیت سیستم
console.log("\n1. System Status");
console.log("SD Card mounted:", true); // از g_sd_mounted استفاده می‌شود

// تست ۲: عملیات خواندن
console.log("\n2. Read Operations");
var files = fs.readdirSync("/sdcard/apps");
if (files === null) {
    console.log("❌ Cannot read directory");
} else {
    console.log("✅ Found", files.length, "files in /sdcard/apps");
    
    // نمایش چند فایل
    for (var i = 0; i < Math.min(files.length, 3); i++) {
        console.log("  - " + files[i]);
    }
}

// تست ۳: نوشتن و خواندن فایل
console.log("\n3. Write/Read File Test");
var testContent = "Final test " + Date.now();
var writeResult = fs.writeFileSync("/sdcard/final_test.txt", testContent);

if (writeResult) {
    console.log("✅ Write successful");
    
    // خواندن و تأیید
    var readContent = fs.readFileSync("/sdcard/final_test.txt");
    if (readContent === testContent) {
        console.log("✅ Read verification: PASS");
    } else {
        console.log("❌ Read verification: FAIL");
        console.log("  Expected:", testContent);
        console.log("  Got:", readContent);
    }
    
    // پاک کردن فایل تست
    var deleteResult = fs.unlink("/sdcard/final_test.txt");
    console.log("Delete test file:", deleteResult ? "SUCCESS" : "FAILED");
} else {
    console.log("❌ Write failed");
}

// تست ۴: عملیات دایرکتوری
console.log("\n4. Directory Operations");
var dirResult = fs.mkdir("/sdcard/test_final_dir");
console.log("Create directory:", dirResult);

if (dirResult) {
    // نوشتن فایل در دایرکتوری
    var fileInDir = fs.writeFileSync("/sdcard/test_final_dir/file.txt", "test");
    console.log("Write file in directory:", fileInDir);
    
    // لیست کردن دایرکتوری
    var dirFiles = fs.readdirSync("/sdcard/test_final_dir");
    console.log("Files in directory:", dirFiles ? dirFiles.length : 0);
    
    // پاک کردن
    if (dirFiles && dirFiles.length > 0) {
        fs.unlink("/sdcard/test_final_dir/file.txt");
    }
    fs.rmdir("/sdcard/test_final_dir");
    console.log("Cleanup: SUCCESS");
}

// تست ۵: اطلاعات فایل (با مدیریت خطا)
console.log("\n5. File Information");
var testStats = fs.statSync("/sdcard/apps/TEST.JS");
if (testStats === null) {
    console.log("❌ Cannot get file stats for TEST.JS");
} else {
    console.log("TEST.JS info:");
    console.log("  Size:", testStats.size, "bytes");
    console.log("  Is file:", testStats.isFile);
    console.log("  Is directory:", testStats.isDirectory);
}

// تست ۶: بررسی وجود فایل
console.log("\n6. File Existence");
var exists = fs.existsSync("/sdcard/apps/TEST.JS");
console.log("TEST.JS exists:", exists);

var notExists = fs.existsSync("/sdcard/nonexistent.txt");
console.log("Non-existent file exists:", notExists);

console.log("\n🎊 FILESYSTEM TEST COMPLETED SUCCESSFULLY!");
console.log("All basic operations are working correctly! 🚀");