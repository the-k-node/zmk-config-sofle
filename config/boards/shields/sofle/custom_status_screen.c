/*
 * Copyright (c) 2020 The ZMK Contributors
 * SPDX-License-Identifier: MIT
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

#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 32

#define HBUF_STRIDE ((DISPLAY_WIDTH + 7) / 8)
#define HBUF_DATA_SIZE (HBUF_STRIDE * DISPLAY_HEIGHT)
#define HBUF_TOTAL_SIZE (8 + HBUF_DATA_SIZE)

/* 32-bit aligned buffer */
static uint8_t frame_buf[HBUF_TOTAL_SIZE] __attribute__((aligned(4)));

static lv_img_dsc_t frame_dsc = {
    .header.cf = LV_IMG_CF_INDEXED_1BIT,
    .header.always_zero = 0,
    .header.reserved = 0,
    .header.w = DISPLAY_WIDTH,
    .header.h = DISPLAY_HEIGHT,
    .data_size = HBUF_TOTAL_SIZE,
    .data = frame_buf,
};

static void init_palette(void) {
    /* Color 0: Black, Color 1: White */
    frame_buf[0] = 0x00; frame_buf[1] = 0x00; frame_buf[2] = 0x00; frame_buf[3] = 0xFF;
    frame_buf[4] = 0xFF; frame_buf[5] = 0xFF; frame_buf[6] = 0xFF; frame_buf[7] = 0xFF;
}

static void draw_bitmap_to_buffer(const uint8_t *src, int src_w, int src_h,
                                  int dst_offset_x, int dst_offset_y) {
    /* Fill with 1s (White) which hardware inverts to Dark */
    uint8_t *bmp = &frame_buf[8];
    memset(bmp, 0xFF, HBUF_DATA_SIZE);

    int src_pages = (src_h + 7) / 8;
    for (int page = 0; page < src_pages; page++) {
        for (int col = 0; col < src_w; col++) {
            uint8_t byte_val = src[col + page * src_w];
            for (int bit = 0; bit < 8; bit++) {
                int src_y = page * 8 + bit;
                if (src_y >= src_h) break;

                if (byte_val & (1 << bit)) {
                    int px_x = dst_offset_x + col;
                    int px_y = dst_offset_y + src_y;

                    if (px_x >= 0 && px_x < DISPLAY_WIDTH &&
                        px_y >= 0 && px_y < DISPLAY_HEIGHT) {
                        /* Set to 0 (Black) which hardware inverts to Lit */
                        int byte_offset = px_y * HBUF_STRIDE + (px_x / 8);
                        int bit_pos = 7 - (px_x % 8);
                        bmp[byte_offset] &= ~(1 << bit_pos);
                    }
                }
            }
        }
    }
}

static uint32_t step_counter = 0;
static lv_obj_t *img_widget = NULL;

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
    return "PERIPHERAL";
#endif
}

static void anim_timer_cb(lv_timer_t *timer) {
    (void)timer;
    step_counter++;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t phase = step_counter % 28;

    if (phase < 15) {
        uint8_t tap_idx = phase % 2;
        draw_bitmap_to_buffer(bongo_tap[tap_idx], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    } else if (phase < 25) {
        uint8_t idle_idx = (phase - 15) % 5;
        draw_bitmap_to_buffer(bongo_idle[idle_idx], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
    } else {
        draw_bitmap_to_buffer(bongo_prep, DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
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
    draw_bitmap_to_buffer(sprite_frame, 32, 22, luna_x, luna_y);
#endif

    /* Safely force LVGL to redraw the image */
    lv_img_cache_invalidate_src(&frame_dsc);
    if (img_widget != NULL) {
        lv_obj_invalidate(img_widget);
    }
}

static void screen_delete_cb(lv_event_t * e) {
    /* When ZMK destroys the screen (e.g. going to sleep), prevent dangling pointer */
    img_widget = NULL;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    
    init_palette();

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    draw_bitmap_to_buffer(bongo_tap[0], DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0);
#else
    int luna_x = (DISPLAY_WIDTH - 32) / 2;
    int luna_y = DISPLAY_HEIGHT - 22;
    draw_bitmap_to_buffer(luna_sit[0], 32, 22, luna_x, luna_y);
#endif

    img_widget = lv_img_create(screen);
    lv_img_set_src(img_widget, &frame_dsc);
    lv_obj_align(img_widget, LV_ALIGN_TOP_LEFT, 0, 0);
    
    /* Attach a deletion handler to prevent crash on sleep */
    lv_obj_add_event_cb(screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, get_active_layer_name());
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 2, 0);
    
    /* Only create the timer once, or if it was destroyed */
    static lv_timer_t * anim_timer = NULL;
    if (anim_timer == NULL) {
        anim_timer = lv_timer_create(anim_timer_cb, 180, NULL);
    }
    
    return screen;
}
