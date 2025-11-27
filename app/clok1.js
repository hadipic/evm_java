console.log("=== IMAGE TEST ON APP SCREEN ===");

// استفاده از صفحه app screen که از قبل در ماژول LVGL ایجاد شده
// نیازی به ایجاد صفحه جدید نیست!

console.log("✅ Using existing app screen");

var test_images = [
    {path: "S:/apps/watch.png", name: "PNG Watch"},
    {path: "S:/apps/hour.png", name: "PNG Hour"}, 
    {path: "S:/apps/minute.png", name: "PNG Minute"},
    {path: "S:/apps/second.png", name: "PNG Second"}
];

// ابتدا یک تست ساده با اشکال پایه
console.log("🎯 Testing basic shapes first...");

// مستطیل تست
var test_rect = lvgl.obj_create(null);
lvgl.obj_set_size(test_rect, 80, 60);
lvgl.obj_align(test_rect, lvgl.ALIGN_CENTER, 0, -20);
lvgl.obj_set_style_bg_color(test_rect, lvgl.color_hex(0xFF0000), 0);
lvgl.obj_set_style_border_color(test_rect, lvgl.color_hex(0xFFFFFF), 0);
lvgl.obj_set_style_border_width(test_rect, 2, 0);

var test_label = lvgl.label_create(null);
lvgl.label_set_text(test_label, "Basic Shape Test");
lvgl.obj_set_style_text_color(test_label, lvgl.color_hex(0xFFFFFF), 0);
lvgl.obj_align(test_label, lvgl.ALIGN_CENTER, 0, 20);

lvgl.refr_now(null);
console.log("✅ Red rectangle displayed - checking if basic LVGL works");
delay(2000);

// پاکسازی
lvgl.obj_del(test_rect);
lvgl.obj_del(test_label);

// حالا تست تصاویر
for (var i = 0; i < test_images.length; i++) {
    console.log("🖼️ Testing: " + test_images[i].name);
    
    if (lvgl.file_exists(test_images[i].path)) {
        console.log("✅ File exists, loading image...");
        
        // شماره تصویر
        var counter_label = lvgl.label_create(null);
        lvgl.label_set_text(counter_label, "Image " + (i+1) + "/" + test_images.length);
        lvgl.obj_set_style_text_color(counter_label, lvgl.color_hex(0x00FF00), 0);
        lvgl.obj_align(counter_label, lvgl.ALIGN_TOP_MID, 0, 5);
        
        // تصویر اصلی
        var img = lvgl.img_create(null);
        lvgl.img_set_src(img, test_images[i].path);
        lvgl.obj_set_size(img, 80, 80);
        lvgl.obj_align(img, lvgl.ALIGN_CENTER, 0, 0);
        
        // نام فایل
        var name_label = lvgl.label_create(null);
        lvgl.label_set_text(name_label, test_images[i].name);
        lvgl.obj_set_style_text_color(name_label, lvgl.color_hex(0xFFFFFF), 0);
        lvgl.obj_align(name_label, lvgl.ALIGN_BOTTOM_MID, 0, -10);
        
        lvgl.refr_now(null);
        console.log("✅ " + test_images[i].name + " displayed");
        delay(2000);
        
        // پاکسازی برای تصویر بعدی
        lvgl.obj_del(counter_label);
        lvgl.obj_del(img);
        lvgl.obj_del(name_label);
        
    } else {
        console.log("❌ File not found: " + test_images[i].path);
        delay(1000);
    }
}

// پایان
var end_msg = lvgl.label_create(null);
lvgl.label_set_text(end_msg, "Image Test Completed\nCheck console for results");
lvgl.obj_set_style_text_color(end_msg, lvgl.color_hex(0x00FF00), 0);
lvgl.obj_align(end_msg, lvgl.ALIGN_CENTER, 0, 0);

lvgl.refr_now(null);
console.log("🏁 Image test completed - app screen will be cleaned automatically");
delay(3000);