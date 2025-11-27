console.log("=== Ultra Simple Clock ===");

// ابتدا مطمئن شویم LVGL کار می‌کند
console.log("🔧 Testing LVGL basics...");

var screen = lvgl.scr_act();
console.log("✅ Screen object: " + screen);

// یک تست ساده - فقط یک لیبل ایجاد کن
var testLabel = lvgl.label_create(screen);
if (!testLabel) {
    console.log("❌ Failed to create label!");
} else {
    lvgl.obj_set_size(testLabel, 150, 20);
    lvgl.obj_align(testLabel, lvgl.ALIGN_CENTER, 0, 0);
    lvgl.obj_set_style_text_color(testLabel, lvgl.color_hex(lvgl.COLOR_WHITE), lvgl.PART_MAIN);
    lvgl.label_set_text(testLabel, "LVGL Works! Starting Clock...");
    console.log("✅ Basic LVGL test passed");

    // کمی صبر کن
    delay(1000);

    // حالا ساعت ساده را ایجاد کن
    console.log("🕐 Creating simple clock...");

    // پاک کردن تست
    lvgl.obj_del(testLabel);
}

// ایجاد ساعت با حداقل اشیاء
function createSimpleClock() {
    // فقط یک لیبل برای نمایش زمان
    var timeLabel = lvgl.label_create(screen);
    lvgl.obj_set_size(timeLabel, 150, 30);
    lvgl.obj_align(timeLabel, lvgl.ALIGN_CENTER, 0, 0);
    lvgl.obj_set_style_text_color(timeLabel, lvgl.color_hex(lvgl.COLOR_CYAN), lvgl.PART_MAIN);
    
    // عنوان
    var titleLabel = lvgl.label_create(screen);
    lvgl.obj_set_size(titleLabel, 150, 20);
    lvgl.obj_align(titleLabel, lvgl.ALIGN_TOP_MID, 0, 5);
    lvgl.obj_set_style_text_color(titleLabel, lvgl.color_hex(lvgl.COLOR_WHITE), lvgl.PART_MAIN);
    lvgl.label_set_text(titleLabel, "Digital Clock");
    
    return timeLabel;
}

var timeLabel = createSimpleClock();
console.log("✅ Simple clock created");

// زمان اولیه
var hours = 9;
var minutes = 50;
var seconds = 0;

function updateTime() {
    seconds++;
    if (seconds >= 60) {
        seconds = 0;
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours = (hours % 12) + 1;
        }
    }
    
    var timeStr = hours + ":" + 
                 (minutes < 10 ? "0" + minutes : minutes) + ":" + 
                 (seconds < 10 ? "0" + seconds : seconds);
    
    lvgl.label_set_text(timeLabel, timeStr);
    
    // لاگ هر 10 ثانیه
    if (seconds % 10 === 0) {
        console.log("⏰ " + timeStr);
    }
}

// راه‌اندازی اولیه
updateTime();
console.log("🎮 Controls: BACK=Exit");

// حلقه اصلی بسیار ساده
var running = true;
var lastUpdate = Timer.getTime();

while (running) {
    var currentTime = Timer.getTime();
    
    // به‌روزرسانی هر ثانیه
    if (currentTime - lastUpdate >= 1000) {
        updateTime();
        lastUpdate = currentTime;
    }
    
    // بررسی دکمه BACK برای خروج
    try {
        var backPressed = gpio.readButton(3); // دکمه BACK
        if (backPressed === 1) {
            console.log("⏹️ Exit requested");
            running = false;
        }
    } catch (e) {
        console.log("Button error: " + e);
    }
    
    // تأخیر مهم برای WDT
    delay(200);
}

console.log("🧹 Cleaning up...");
lvgl.cleanup_app();
console.log("✅ Clock stopped normally");