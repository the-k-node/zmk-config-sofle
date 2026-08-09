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

/* ── Animation Timing ──────────────────────────────────────────────── */
#define ANIM_FRAME_DURATION_MS 180

/* ── Frame buffer for decoded horizontal-scan bitmap ───────────────── */
#define HBUF_STRIDE ((DISPLAY_WIDTH + 7) / 8)
#define HBUF_DATA_SIZE (HBUF_STRIDE * DISPLAY_HEIGHT)
#define HBUF_TOTAL_SIZE (8 + HBUF_DATA_SIZE)

static uint8_t frame_buf[HBUF_TOTAL_SIZE];

/* ── LVGL image descriptors (Swapped to bypass cache) ──────────────── */
static lv_img_dsc_t frame_dsc[2] = {
    {
        .header.cf = LV_IMG_CF_INDEXED_1BIT,
        .header.always_zero = 0,
        .header.reserved = 0,
        .header.w = DISPLAY_WIDTH,
        .header.h = DISPLAY_HEIGHT,
        .data_size = HBUF_TOTAL_SIZE,
        .data = frame_buf,
    },
    {
        .header.cf = LV_IMG_CF_INDEXED_1BIT,
        .header.always_zero = 0,
        .header.reserved = 0,
        .header.w = DISPLAY_WIDTH,
        .header.h = DISPLAY_HEIGHT,
        .data_size = HBUF_TOTAL_SIZE,
        .data = frame_buf,
    }
};

/* ── UI Widgets & State ────────────────────────────────────────────── */
static uint32_t step_counter = 0;
static lv_obj_t *img_widget = NULL;
static lv_obj_t *layer_label = NULL;
static lv_obj_t *battery_label = NULL;

/*
 * Convert a vertical-scan QMK bitmap into LVGL INDEXED_1BIT format.
 */
static void decode_vertical_to_indexed1bit(const uint8_t *src, int src_w, int src_h,
                                           int dst_offset_x, int dst_offset_y) {
    /* Fill with 0xFF (1s) -> Dark background on inverted OLED */
    memset(frame_buf, 0xFF, HBUF_TOTAL_SIZE);
    
    /* Standard palette */
    frame_buf[0] = 0x00; frame_buf[1] = 0x00; frame_buf[2] = 0x00; frame_buf[3] = 0xFF;
    frame_buf[4] = 0xFF; frame_buf[5] = 0xFF; frame_buf[6] = 0xFF; frame_buf[7] = 0xFF;

    int src_pages = (src_h + 7) / 8;
    uint8_t *bmp = &frame_buf[8];

    for (int page = 0; page < src_pages; page++) {
        for (int col = 0; col < src_w; col++) {
            uint8_t byte_val = src[col + page * src_w];
            for (int bit = 0; bit < 8; bit++) {
                int src_y = page * 8 + bit;
                if (src_y >= src_h) break;

                if (!(byte_val & (1 << bit))) continue;

                int px_x = dst_offset_x + col;
                int px_y = dst_offset_y + src_y;

                if (px_x < 0 || px_x >= DISPLAY_WIDTH ||
                    px_y < 0 || px_y >= DISPLAY_HEIGHT)
                    continue;

                /* Clear to 0 -> Lit pixel on inverted OLED */
                int byte_offset = px_y * HBUF_STRIDE + (px_x / 8);
                int bit_pos = 7 - (px_x % 8);
                bmp[byte_offset] &= ~(1 << bit_pos);
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
        decode_vertical_to_indexed1bit(bongo_tap[tap_idx], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    } else if (phase < 25) {
        uint8_t idle_idx = (phase - 15) % 5;
        decode_vertical_to_indexed1bit(bongo_idle[idle_idx], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    } else {
        decode_vertical_to_indexed1bit(bongo_prep, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
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

    decode_vertical_to_indexed1bit(sprite_frame, 32, 22, luna_x, luna_y);
#endif

    /* Force LVGL image update by swapping descriptor pointers */
    uint8_t active_desc = step_counter % 2;
    lv_img_set_src(img_widget, &frame_dsc[active_desc]);

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

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    decode_vertical_to_indexed1bit(bongo_tap[0], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
#else
    {
        int luna_x = (DISPLAY_WIDTH - 32) / 2;
        int luna_y = DISPLAY_HEIGHT - 22;
        decode_vertical_to_indexed1bit(luna_sit[0], 32, 22, luna_x, luna_y);
    }
#endif

    img_widget = lv_img_create(screen);
    lv_img_set_src(img_widget, &frame_dsc[0]);
    lv_obj_align(img_widget, LV_ALIGN_TOP_LEFT, 0, 0);

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
