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

static const char *get_active_layer_name(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return "CENTRAL";
#else
    return "PERIPHERAL";
#endif
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

    lv_obj_t *img_widget = lv_img_create(screen);
    lv_img_set_src(img_widget, &frame_dsc);
    lv_obj_align(img_widget, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, get_active_layer_name());
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 2, 0);
    
    return screen;
}
