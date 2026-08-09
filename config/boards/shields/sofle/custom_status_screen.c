/*
 * Copyright (c) 2020 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom OLED status screen:
 *  - Animated Bongo Cat (Left/Central) & Luna Pet (Right/Peripheral)
 *  - Active Layer Name & Battery Status widgets overlaid at the top
 */

#include <zephyr/kernel.h>
#include <zmk/display.h>
#include <lvgl.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/keymap.h>
#endif

#if __has_include(<zmk/battery.h>)
#include <zmk/battery.h>
#endif

#include "bongo_cat.h"
#include "luna.h"

/* ── Display Constants ─────────────────────────────────────────────── */
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 32
#define ANIM_FRAME_DURATION_MS 180

/* ── UI Widgets & State ────────────────────────────────────────────── */
static uint32_t step_counter = 0;
static lv_obj_t *canvas_widget = NULL;
static lv_obj_t *layer_label = NULL;
static lv_obj_t *battery_label = NULL;

/* In LVGL 7, 1-bit indexed canvas requires this buffer size macro */
#define CANVAS_BUF_SIZE LV_CANVAS_BUF_SIZE_INDEXED_1BIT(DISPLAY_WIDTH, DISPLAY_HEIGHT)
static uint8_t canvas_buf[CANVAS_BUF_SIZE];

/*
 * Draw a vertical-scan QMK bitmap directly onto the LVGL canvas
 */
static void draw_bitmap_to_canvas(const uint8_t *src, int src_w, int src_h,
                                  int dst_offset_x, int dst_offset_y) {
    /* Fill canvas with background color */
    lv_canvas_fill_bg(canvas_widget, lv_color_black(), LV_OPA_COVER);

    int src_pages = (src_h + 7) / 8;
    
    for (int page = 0; page < src_pages; page++) {
        for (int col = 0; col < src_w; col++) {
            uint8_t byte_val = src[col + page * src_w];
            for (int bit = 0; bit < 8; bit++) {
                int src_y = page * 8 + bit;
                if (src_y >= src_h) break;

                /* Only draw lit pixels */
                if (byte_val & (1 << bit)) {
                    int px_x = dst_offset_x + col;
                    int px_y = dst_offset_y + src_y;

                    if (px_x >= 0 && px_x < DISPLAY_WIDTH &&
                        px_y >= 0 && px_y < DISPLAY_HEIGHT) {
                        lv_canvas_set_px(canvas_widget, px_x, px_y, lv_color_white());
                    }
                }
            }
        }
    }
}

/* ── Helper: Get active layer name ─────────────────────────────────── */
static const char *get_active_layer_name(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint8_t layer = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(layer);
    if (name && strlen(name) > 0) {
        return name;
    }
    switch (layer) {
        case 0: return "BASE";
        case 1: return "LOWER";
        case 2: return "RAISE";
        case 3: return "ADJUST";
        default: return "LAYER";
    }
#else
    return "RIGHT";
#endif
}

/* ── Helper: Get battery level percentage ──────────────────────────── */
static uint8_t get_battery_level(void) {
#if __has_include(<zmk/battery.h>)
    return zmk_battery_state_of_charge();
#else
    return 100;
#endif
}

/* ── Animation & Widget Timer Callback ─────────────────────────────── */
static void anim_timer_cb(lv_timer_t *timer) {
    (void)timer;
    step_counter++;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t phase = step_counter % 28;

    if (phase < 15) {
        uint8_t tap_idx = phase % 2;
        draw_bitmap_to_canvas(bongo_tap[tap_idx], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    } else if (phase < 25) {
        uint8_t idle_idx = (phase - 15) % 5;
        draw_bitmap_to_canvas(bongo_idle[idle_idx], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    } else {
        draw_bitmap_to_canvas(bongo_prep, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    }
#else
    uint32_t phase = step_counter % 56;
    const uint8_t *sprite_frame;

    if (phase < 12) {
        sprite_frame = luna_sit[phase % 2];
    } else if (phase < 24) {
        sprite_frame = luna_walk[(phase - 12) % 2];
    } else if (phase < 32) {
        sprite_frame = luna_bark[(phase - 24) % 2];
    } else if (phase < 44) {
        sprite_frame = luna_run[(phase - 32) % 2];
    } else {
        sprite_frame = luna_sneak[(phase - 44) % 2];
    }

    int luna_x = (DISPLAY_WIDTH - 32) / 2;
    int luna_y = DISPLAY_HEIGHT - 22;

    draw_bitmap_to_canvas(sprite_frame, 32, 22, luna_x, luna_y);
#endif

    /* Force redraw */
    lv_obj_invalidate(canvas_widget);

    if (layer_label) {
        lv_label_set_text(layer_label, get_active_layer_name());
    }

    if (battery_label) {
        char bat_buf[16];
        snprintf(bat_buf, sizeof(bat_buf), "BAT %d%%", get_battery_level());
        lv_label_set_text(battery_label, bat_buf);
    }
}

/* ── ZMK Display Entry Point ───────────────────────────────────────── */
lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* Initialize Canvas */
    canvas_widget = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas_widget, canvas_buf, DISPLAY_WIDTH, DISPLAY_HEIGHT, LV_IMG_CF_INDEXED_1BIT);
    
    /* ZMK monochrome theme defaults: 0 is black (bg), 1 is white (fg) */
    lv_canvas_set_palette(canvas_widget, 0, lv_color_black());
    lv_canvas_set_palette(canvas_widget, 1, lv_color_white());
    lv_obj_align(canvas_widget, LV_ALIGN_TOP_LEFT, 0, 0);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    draw_bitmap_to_canvas(bongo_tap[0], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
#else
    {
        int luna_x = (DISPLAY_WIDTH - 32) / 2;
        int luna_y = DISPLAY_HEIGHT - 22;
        draw_bitmap_to_canvas(luna_sit[0], 32, 22, luna_x, luna_y);
    }
#endif

    /* Initialize labels */
    layer_label = lv_label_create(screen);
    lv_obj_align(layer_label, LV_ALIGN_TOP_LEFT, 2, 0);
    lv_label_set_text(layer_label, get_active_layer_name());

    battery_label = lv_label_create(screen);
    lv_obj_align(battery_label, LV_ALIGN_TOP_RIGHT, -2, 0);
    char bat_buf[16];
    snprintf(bat_buf, sizeof(bat_buf), "BAT %d%%", get_battery_level());
    lv_label_set_text(battery_label, bat_buf);

    lv_timer_create(anim_timer_cb, ANIM_FRAME_DURATION_MS, NULL);

    return screen;
}
