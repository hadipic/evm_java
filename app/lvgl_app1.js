print("LVGL Demo Started (EVM Compatible)");

// ===== لود LVGL =====

// شمارنده
var counter = 0;

// ===== صفحه اصلی =====
print(">>> Get screen");
var scr = lv.lv_scr_act();

// ===== کانتینر =====
print(">>> Create container");
var cont = lv.lv_obj_create(scr);
lv.lv_obj_set_size(cont, 200, 150);
lv.lv_obj_align(cont, lv.LV_ALIGN_CENTER, 0, 0);

// ===== عنوان =====
print(">>> Create title label");
var title = lv.lv_label_create(cont);
lv.lv_label_set_text(title, "LVGL Demo");
lv.lv_obj_set_pos(title, 10, 5);

// ===== شمارنده =====
print(">>> Create counter label");
var counter_label = lv.lv_label_create(cont);
lv.lv_label_set_text(counter_label, "Count: 0");
lv.lv_obj_set_pos(counter_label, 10, 25);

// ===== تابع ساخت دکمه =====
function make_btn(parent, x, y, w, h, text) {
    print(">>> Make button:", text);
    var btn = lv.lv_btn_create(parent);
    lv.lv_obj_set_size(btn, w, h);
    lv.lv_obj_set_pos(btn, x, y);

    var lbl = lv.lv_label_create(btn);
    lv.lv_label_set_text(lbl, text);
    lv.lv_obj_center(lbl);
    return btn;
}

// ===== دکمه‌ها =====
var btn_inc = make_btn(cont, 10, 50, 60, 25, "+1");
var btn_dec = make_btn(cont, 80, 50, 60, 25, "-1");
var btn_reset = make_btn(cont, 10, 85, 130, 25, "Reset");

// ===== هندلر دکمه‌ها =====
function ev_inc(obj, event_code) {
    if (event_code === lv.LV_EVENT_CLICKED) {
        counter++;
        lv.lv_label_set_text(counter_label, "Count: " + counter);
        print("Counter increased to: " + counter);
    }
}

function ev_dec(obj, event_code) {
    if (event_code === lv.LV_EVENT_CLICKED) {
        counter--;
        lv.lv_label_set_text(counter_label, "Count: " + counter);
        print("Counter decreased to: " + counter);
    }
}

function ev_reset(obj, event_code) {
    if (event_code === lv.LV_EVENT_CLICKED) {
        counter = 0;
        lv.lv_label_set_text(counter_label, "Count: " + counter);
        print("Counter reset to: " + counter);
    }
}

// ===== اتصال هندلرها =====
lv.lv_obj_add_event_cb(btn_inc, ev_inc, lv.LV_EVENT_CLICKED, null);
lv.lv_obj_add_event_cb(btn_dec, ev_dec, lv.LV_EVENT_CLICKED, null);
lv.lv_obj_add_event_cb(btn_reset, ev_reset, lv.LV_EVENT_CLICKED, null);

print("UI Created Successfully!");
print("Click the buttons to test!");
