// LVGL Test 2 - Simple UI
console.log("🚀 Starting LVGL UI Test");

var screen_width = lvgl.disp_get_hor_res();
var screen_height = lvgl.disp_get_ver_res();
console.log("📱 Screen:", screen_width, "x", screen_height);

// ایجاد کانتینر اصلی
var container = lvgl.obj_create();
lvgl.obj_set_size(container, 140, 100);
lvgl.obj_set_pos(container, 10, 10);
lvgl.obj_set_style_local_bg_color(container, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_BLUE);
lvgl.obj_set_style_local_radius(container, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, 10);

// ایجاد عنوان
var title = lvgl.label_create(container);
lvgl.label_set_text(title, "LVGL Test");
lvgl.obj_set_pos(title, 10, 10);
lvgl.label_set_align(title, lvgl.LABEL_ALIGN_CENTER);

// ایجاد متن اطلاعات
var info = lvgl.label_create(container);
lvgl.label_set_text(info, "Size: " + screen_width + "x" + screen_height);
lvgl.obj_set_pos(info, 10, 30);

// ایجاد دکمه
var btn = lvgl.btn_create();
lvgl.obj_set_size(btn, 80, 30);
lvgl.obj_align_simple(btn, lvgl.ALIGN_IN_BOTTOM_MID, 0, -10);
lvgl.obj_set_style_local_bg_color(btn, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, lvgl.COLOR_RED);
lvgl.obj_set_style_local_radius(btn, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, 5);

var btn_label = lvgl.label_create(btn);
lvgl.label_set_text(btn_label, "Click Me!");
lvgl.label_set_align(btn_label, lvgl.LABEL_ALIGN_CENTER);

console.log("✅ UI Created");

// نگه داشتن برنامه برای نمایش
for (var i = 0; i < 10; i++) {
    console.log("Running...", i);
    delay(1000);
}

// تمیزکاری
lvgl.obj_del(container);
lvgl.obj_del(btn);
console.log("✅ Cleanup done");