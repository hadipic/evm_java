// LVGL Test 3 - Colors and Styles
console.log("🎨 LVGL Colors and Styles Test");

// ایجاد چند آبجکت با رنگ‌های مختلف
var colors = [
    { name: "RED", value: lvgl.COLOR_RED },
    { name: "GREEN", value: lvgl.COLOR_GREEN },
    { name: "BLUE", value: lvgl.COLOR_BLUE },
    { name: "YELLOW", value: lvgl.COLOR_YELLOW },
    { name: "PURPLE", value: lvgl.COLOR_PURPLE }
];

var objects = [];

for (var i = 0; i < colors.length; i++) {
    var obj = lvgl.obj_create();
    lvgl.obj_set_size(obj, 25, 25);
    lvgl.obj_set_pos(obj, 10 + i * 30, 10);
    lvgl.obj_set_style_local_bg_color(obj, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, colors[i].value);
    lvgl.obj_set_style_local_radius(obj, lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, 5);
    
    objects.push(obj);
    console.log("✅ Created", colors[i].name, "object");
}

// ایجاد لیبل برای نمایش اطلاعات
var info_label = lvgl.label_create();
lvgl.label_set_text(info_label, "LVGL Colors Test\n5 colored objects");
lvgl.obj_set_pos(info_label, 10, 50);
lvgl.label_set_align(info_label, lvgl.LABEL_ALIGN_LEFT);

console.log("✅ All objects created");

// انیمیشن ساده - تغییر شفافیت
for (var frame = 0; frame < 5; frame++) {
    for (var i = 0; i < objects.length; i++) {
        var opa = (frame % 2 === 0) ? lvgl.OPA_100 : lvgl.OPA_50;
        lvgl.obj_set_style_local_bg_opa(objects[i], lvgl.OBJ_PART_MAIN, lvgl.STATE_DEFAULT, opa);
    }
    console.log("Frame", frame);
    delay(500);
}

// تمیزکاری
for (var i = 0; i < objects.length; i++) {
    lvgl.obj_del(objects[i]);
}
lvgl.obj_del(info_label);

console.log("✅ Colors test completed");