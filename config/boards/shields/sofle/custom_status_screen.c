/*
 * Copyright (c) 2020 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zmk/display.h>
#include <lvgl.h>

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "DEBUG OK");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    
    return screen;
}
