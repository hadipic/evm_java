// LVGL Test 1 - Basic Test
console.log("🚀 Starting LVGL Basic Test");

// بررسی وجود ماژول LVGL
if (typeof lvgl === 'undefined') {
    console.log("❌ LVGL module not found!");
} else {
    console.log("✅ LVGL module is available");
    
    // گرفتن اندازه صفحه
    var screen_width = lvgl.disp_get_hor_res();
    var screen_height = lvgl.disp_get_ver_res();
    console.log("📱 Screen size:", screen_width, "x", screen_height);
    
    // ایجاد یک آبجکت ساده
    var obj = lvgl.obj_create();
    lvgl.obj_set_size(obj, 100, 50);
    lvgl.obj_set_pos(obj, 30, 40);
    lvgl.obj_set_style_local_bg_color(obj, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_RED);
    
    console.log("✅ Basic object created");
    
    delay(2000);
    
    // تغییر رنگ
    lvgl.obj_set_style_local_bg_color(obj, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_BLUE);
    console.log("✅ Color changed to blue");
    
    delay(2000);
    
    // حذف آبجکت
    lvgl.obj_del(obj);
    console.log("✅ Object deleted");
}

console.log("✅ LVGL Basic Test Completed");