console.log("=== FILE SYSTEM DEBUG ===");

// تست مسیرهای مختلف
var test_paths = [
    "/sdcard/apps/watch.png",
    "/sdcard/apps/hour.png",
    "/sdcard/apps/minute.png", 
    "/sdcard/apps/second.png",
    "/sdcard/speaker0.bmp",
    "/sdcard/image.bmp"
];

console.log("🔍 Checking files with file_exists:");
for (var i = 0; i < test_paths.length; i++) {
    var exists = lvgl.file_exists(test_paths[i]);
    console.log("   " + test_paths[i] + " -> " + (exists ? "✅" : "❌"));
}

// لیست فایل‌های موجود
console.log("📂 Listing files in /sdcard:");
var files = lvgl.list_files("/sdcard");
if (files && files.length > 0) {
    for (var i = 0; i < files.length; i++) {
        console.log("   " + files[i]);
    }
} else {
    console.log("   No files found!");
}

console.log("📂 Listing files in /sdcard/apps:");
var app_files = lvgl.list_files("/sdcard/apps");
if (app_files && app_files.length > 0) {
    for (var i = 0; i < app_files.length; i++) {
        console.log("   " + app_files[i]);
    }
} else {
    console.log("   No files in apps directory!");
}