// LVGL Test 5 - Button Interaction
console.log("🔘 LVGL Button Test");

var counter = 0;

// ایجاد لیبل برای نمایش شمارنده
var counter_label = lvgl.label_create();
lvgl.label_set_text(counter_label, "Counter: " + counter);
lvgl.obj_align_simple(counter_label, lvgl.ALIGN_IN_TOP_MID, 0, 10);

// ایجاد دکمه برای افزایش
var inc_btn = lvgl.btn_create();
lvgl.obj_set_size(inc_btn, 100, 40);
lvgl.obj_align_simple(inc_btn, lvgl.ALIGN_CENTER, 0, -30);
lvgl.obj_set_style_local_bg_color(inc_btn, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_GREEN);

var inc_label = lvgl.label_create(inc_btn);
lvgl.label_set_text(inc_label, "Increase");
lvgl.label_set_align(inc_label, lvgl.LABEL_ALIGN_CENTER);

// ایجاد دکمه برای کاهش
var dec_btn = lvgl.btn_create();
lvgl.obj_set_size(dec_btn, 100, 40);
lvgl.obj_align_simple(dec_btn, lvgl.ALIGN_CENTER, 0, 30);
lvgl.obj_set_style_local_bg_color(dec_btn, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_RED);

var dec_label = lvgl.label_create(dec_btn);
lvgl.label_set_text(dec_label, "Decrease");
lvgl.label_set_align(dec_label, lvgl.LABEL_ALIGN_CENTER);

console.log("✅ Buttons created - Use SELECT to simulate clicks");

// شبیه‌سازی کلیک دکمه با دکمه SELECT
var running = true;
var click_count = 0;

while (running && click_count < 10) {
    // بررسی دکمه SELECT (شبیه‌سازی کلیک)
    if (readButton(1)) { // دکمه SELECT
        counter++;
        lvgl.label_set_text(counter_label, "Counter: " + counter);
        console.log("➕ Counter increased to:", counter);
        delay(500); // جلوگیری از کلیک‌های متوالی
        click_count++;
    }
    
    // بررسی دکمه BACK برای خروج
    if (readButton(3)) { // دکمه BACK
        console.log("⏹️ Exit requested");
        running = false;
    }
    
    delay(100);
}

// تمیزکاری
lvgl.obj_del(counter_label);
lvgl.obj_del(inc_btn);
lvgl.obj_del(dec_btn);

console.log("✅ Button test completed. Final counter:", counter);