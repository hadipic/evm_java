// LVGL Demo - Fixed Version for LVGL v8 with Correct GPIO API
console.log("🎮 Starting LVGL Demo with Button Control - LVGL v8");

var counter = 0;
var current_selection = 0; // 0: Increase, 1: Decrease, 2: Reset
var total_buttons = 3;
var running = true;

// ==================== ایجاد رابط کاربری ====================

// ایجاد کانتینر اصلی
var container = lvgl.obj_create();
lvgl.obj_set_size(container, 150, 120);
lvgl.obj_set_pos(container, 5, 5);
lvgl.obj_set_style_bg_color(container, lvgl.COLOR_BLUE, lvgl.PART_MAIN);
lvgl.obj_set_style_radius(container, 10, lvgl.PART_MAIN);

// عنوان برنامه
var title_label = lvgl.label_create(container);
lvgl.label_set_text(title_label, "LVGL Demo v8");
lvgl.obj_set_pos(title_label, 10, 5);
lvgl.obj_set_style_text_color(title_label, lvgl.COLOR_WHITE, lvgl.PART_MAIN);

// نمایش شمارنده
var counter_label = lvgl.label_create(container);
lvgl.label_set_text(counter_label, "Count: " + counter);
lvgl.obj_set_pos(counter_label, 10, 25);
lvgl.obj_set_style_text_color(counter_label, lvgl.COLOR_WHITE, lvgl.PART_MAIN);

// ==================== ایجاد دکمه‌ها ====================

var buttons = [];

// دکمه افزایش
var inc_btn = lvgl.btn_create(container);
lvgl.obj_set_size(inc_btn, 60, 25);
lvgl.obj_set_pos(inc_btn, 10, 50);
lvgl.obj_set_style_bg_color(inc_btn, lvgl.COLOR_GREEN, lvgl.PART_MAIN);

var inc_label = lvgl.label_create(inc_btn);
lvgl.label_set_text(inc_label, "+1");
lvgl.obj_set_style_text_align(inc_label, lvgl.TEXT_ALIGN_CENTER, 0);
lvgl.obj_align(inc_label, lvgl.ALIGN_CENTER, 0, 0);

buttons.push(inc_btn);

// دکمه کاهش
var dec_btn = lvgl.btn_create(container);
lvgl.obj_set_size(dec_btn, 60, 25);
lvgl.obj_set_pos(dec_btn, 80, 50);
lvgl.obj_set_style_bg_color(dec_btn, lvgl.COLOR_RED, lvgl.PART_MAIN);

var dec_label = lvgl.label_create(dec_btn);
lvgl.label_set_text(dec_label, "-1");
lvgl.obj_set_style_text_align(dec_label, lvgl.TEXT_ALIGN_CENTER, 0);
lvgl.obj_align(dec_label, lvgl.ALIGN_CENTER, 0, 0);

buttons.push(dec_btn);

// دکمه ریست
var reset_btn = lvgl.btn_create(container);
lvgl.obj_set_size(reset_btn, 130, 25);
lvgl.obj_set_pos(reset_btn, 10, 85);
lvgl.obj_set_style_bg_color(reset_btn, lvgl.COLOR_YELLOW, lvgl.PART_MAIN);

var reset_label = lvgl.label_create(reset_btn);
lvgl.label_set_text(reset_label, "Reset");
lvgl.obj_set_style_text_align(reset_label, lvgl.TEXT_ALIGN_CENTER, 0);
lvgl.obj_align(reset_label, lvgl.ALIGN_CENTER, 0, 0);

buttons.push(reset_btn);

// ==================== نمایش تصویر ساده ====================

var image_obj = lvgl.obj_create();
lvgl.obj_set_size(image_obj, 50, 50);
lvgl.obj_set_pos(image_obj, 10, 115);
lvgl.obj_set_style_bg_color(image_obj, lvgl.COLOR_PURPLE, lvgl.PART_MAIN);
lvgl.obj_set_style_radius(image_obj, 5, lvgl.PART_MAIN);

var img_label = lvgl.label_create(image_obj);
lvgl.label_set_text(img_label, "IMG");
lvgl.obj_set_style_text_align(img_label, lvgl.TEXT_ALIGN_CENTER, 0);
lvgl.obj_align(img_label, lvgl.ALIGN_CENTER, 0, 0);

console.log("✅ UI created successfully");

// ==================== تابع برجسته کردن دکمه انتخاب شده ====================

function highlightSelectedButton() {
    for (var i = 0; i < buttons.length; i++) {
        if (i === current_selection) {
            // دکمه انتخاب شده - حاشیه روشن
            lvgl.obj_set_style_border_color(buttons[i], lvgl.COLOR_WHITE, lvgl.PART_MAIN);
            lvgl.obj_set_style_border_width(buttons[i], 2, lvgl.PART_MAIN);
        } else {
            // دکمه غیرفعال - بدون حاشیه
            lvgl.obj_set_style_border_width(buttons[i], 0, lvgl.PART_MAIN);
        }
    }
}

// برجسته کردن اولیه
highlightSelectedButton();

// ==================== راهنمای کاربر ====================

console.log("🎮 Control Guide:");
console.log("  ▲ UP: Navigate up (GPIO 2)");
console.log("  ▼ DOWN: Navigate down (GPIO 34)"); 
console.log("  ⏎ SELECT: Press selected button (GPIO 4)");
console.log("  ◀ BACK: Exit demo (GPIO 35)");

// ==================== حلقه اصلی برنامه ====================

while (running) {
    // خواندن وضعیت دکمه‌ها با استفاده از API صحیح GPIO
    var up_pressed = false;
    var select_pressed = false;
    var down_pressed = false; 
    var back_pressed = false;
    
    try {
        // روش 1: استفاده از gpio.readButton() با ID دکمه
        if (typeof gpio !== 'undefined' && typeof gpio.readButton === 'function') {
            up_pressed = gpio.readButton(gpio.BUTTON_UP) === 1;     // UP
            select_pressed = gpio.readButton(gpio.BUTTON_SELECT) === 1; // SELECT
            down_pressed = gpio.readButton(gpio.BUTTON_DOWN) === 1;  // DOWN
            back_pressed = gpio.readButton(gpio.BUTTON_BACK) === 1;  // BACK
        }
        // روش 2: استفاده از gpio.read() مستقیم با شماره پین
        else if (typeof gpio !== 'undefined' && typeof gpio.read === 'function') {
            // دکمه‌ها active-low هستند (0 = فشرده)
            up_pressed = gpio.read(2) === 0;     // UP
            select_pressed = gpio.read(4) === 0; // SELECT
            down_pressed = gpio.read(34) === 0;  // DOWN
            back_pressed = gpio.read(35) === 0;  // BACK
        }
    } catch (e) {
        console.log("⚠️ Button reading error:", e);
    }
    
    // مدیریت ناوبری با UP/DOWN
    if (up_pressed) {
        current_selection = (current_selection - 1 + total_buttons) % total_buttons;
        highlightSelectedButton();
        console.log("▲ Selected:", getButtonName(current_selection));
        delay(200); // جلوگیری از ناوبری سریع
    }
    
    if (down_pressed) {
        current_selection = (current_selection + 1) % total_buttons;
        highlightSelectedButton();
        console.log("▼ Selected:", getButtonName(current_selection));
        delay(200);
    }
    
    // اجرای عمل دکمه انتخاب شده با SELECT
    if (select_pressed) {
        console.log("⏎ Pressed:", getButtonName(current_selection));
        
        switch(current_selection) {
            case 0: // Increase
                counter++;
                lvgl.label_set_text(counter_label, "Count: " + counter);
                console.log("➕ Counter:", counter);
                break;
                
            case 1: // Decrease
                counter--;
                lvgl.label_set_text(counter_label, "Count: " + counter);
                console.log("➖ Counter:", counter);
                break;
                
            case 2: // Reset
                counter = 0;
                lvgl.label_set_text(counter_label, "Count: " + counter);
                console.log("🔄 Counter reset to 0");
                break;
        }
        
        delay(300);
    }
    
    // خروج با BACK
    if (back_pressed) {
        console.log("⏹️ Exit requested");
        running = false;
    }
    
    delay(50); // کاهش مصرف CPU
}

// ==================== تابع کمکی برای نام دکمه ====================

function getButtonName(index) {
    var names = ["Increase", "Decrease", "Reset"];
    return names[index];
}

// ==================== تمیزکاری ====================

console.log("🧹 Cleaning up...");

// استفاده از cleanup صحیح برای LVGL v8
if (typeof lvgl !== 'undefined' && typeof lvgl.cleanup_app === 'function') {
    lvgl.cleanup_app();
} else {
    console.log("⚠️ lvgl.cleanup_app not available, manual cleanup...");
    // حذف دستی آبجکت‌ها
    lvgl.obj_del(container);
    lvgl.obj_del(image_obj);
}

console.log("✅ LVGL Demo completed. Final count:", counter);