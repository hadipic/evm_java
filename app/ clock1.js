console.log("=== Simple Clock Test ===");

// تنظیمات پایه
var CENTER_X = 80;
var CENTER_Y = 64;
var appRunning = true;

// ایجاد صفحه
var screen = lvgl.scr_act();
lvgl.obj_set_style_local_bg_color(screen, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_BLACK);

// عنوان
var title = lvgl.label_create(screen);
lvgl.obj_align_simple(title, lvgl.ALIGN_IN_TOP_MID, 0, 5);
lvgl.label_set_text(title, "Simple Clock Test");

// ایجاد عقربه‌ها
var hourHand = lvgl.line_create(screen);
var minHand = lvgl.line_create(screen); 
var secHand = lvgl.line_create(screen);

// استایل عقربه‌ها
lvgl.obj_set_style_local_line_width(hourHand, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, 4);
lvgl.obj_set_style_local_line_color(hourHand, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_GREEN);

lvgl.obj_set_style_local_line_width(minHand, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, 3);
lvgl.obj_set_style_local_line_color(minHand, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_BLUE);

lvgl.obj_set_style_local_line_width(secHand, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, 2);
lvgl.obj_set_style_local_line_color(secHand, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_RED);

// تست موقعیت‌های مختلف
function testPosition(angle, hand, length, name) {
    var endX = CENTER_X + Math.sin(angle) * length;
    var endY = CENTER_Y - Math.cos(angle) * length;
    
    lvgl.line_set_points(hand, CENTER_X, CENTER_Y, Math.round(endX), Math.round(endY));
    console.log(name + " - Angle: " + (angle * 180/Math.PI).toFixed(1) + "°");
}

// تست ۴ موقعیت اصلی
console.log("Testing 4 main positions...");

// موقعیت ۱۲:۰۰ (۰ درجه)
testPosition(0 * Math.PI / 180, hourHand, 15, "12:00");
Timer.delay(1000);

// موقعیت ۳:۰۰ (۹۰ درجه)
testPosition(90 * Math.PI / 180, minHand, 20, "3:00");
Timer.delay(1000);

// موقعیت ۶:۰۰ (۱۸۰ درجه)
testPosition(180 * Math.PI / 180, secHand, 25, "6:00");
Timer.delay(1000);

// موقعیت ۹:۰۰ (۲۷۰ درجه)
testPosition(270 * Math.PI / 180, hourHand, 15, "9:00");
Timer.delay(1000);

console.log("All tests completed!");

// پاک‌سازی وقتی دکمه BACK زده شد
var pollId = Timer.setInterval(function() {
    if (gpio.readButton(3) === 1) {
        Timer.clearInterval(pollId);
        console.log("Cleaning up...");
        lvgl.cleanup_app();
    }
}, 200);



