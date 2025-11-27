print('test image lvgl');

var lv = require('@native.lvgl');
var image = require('@native.image'); // ماژول image جداگانه

// بررسی وجود فایل
print("Checking for logo.png...");

var scr = lv.lv_scr_act();

// ایجاد image widget
var img = lv.lv_img_create(scr);
lv.lv_obj_align(img, lv.LV_ALIGN_CENTER, 0, 0);

try {
    // decode کردن تصویر
    print("Decoding PNG...");
    var png = image.png_decode("logo.png");
    print("PNG decoded successfully");
    
    // تنظیم منبع تصویر
    lv.lv_img_set_src(img, png);
    print("Image source set successfully");
    
} catch (e) {
    print("Error loading image: " + e);
    
    // اگر تصویر لود نشد، یک لیبل جایگزین نمایش دهید
    var label = lv.lv_label_create(scr);
    lv.lv_label_set_text(label, "Image not found\nlogo.png");
    lv.lv_obj_align(label, lv.LV_ALIGN_CENTER, 0, 0);
}

print("Image test completed");
