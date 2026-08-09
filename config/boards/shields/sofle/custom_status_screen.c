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

/* The physical dimensions of the vertical tower */
#define TOWER_WIDTH  32
#define TOWER_HEIGHT 128

/* The native hardware driver dimensions (must not be changed to prevent corruption) */
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 32

/* Canvas buffer for composing the upright tower image */
static uint8_t canvas_buf[LV_CANVAS_BUF_SIZE_INDEXED_1BIT(TOWER_WIDTH, TOWER_HEIGHT)] __attribute__((aligned(4)));

/* The final rotated buffer that Zephyr driver accepts */
#define HBUF_STRIDE ((DISPLAY_WIDTH + 7) / 8)
#define HBUF_DATA_SIZE (HBUF_STRIDE * DISPLAY_HEIGHT)
#define HBUF_TOTAL_SIZE (8 + HBUF_DATA_SIZE)
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

static void init_frame_palette(void) {
    /* Color 0: Black, Color 1: White */
    frame_buf[0] = 0x00; frame_buf[1] = 0x00; frame_buf[2] = 0x00; frame_buf[3] = 0xFF;
    frame_buf[4] = 0xFF; frame_buf[5] = 0xFF; frame_buf[6] = 0xFF; frame_buf[7] = 0xFF;
}

static void draw_bongo_to_canvas(const uint8_t *src) {
    for (int page = 0; page < 4; page++) {
        for (int col = 0; col < 128; col++) {
            uint8_t byte_val = src[col + page * 128];
            for (int bit = 0; bit < 8; bit++) {
                int src_y = page * 8 + bit;
                if (byte_val & (1 << bit)) {
                    // Transpose bongo cat to fit upright on tower
                    int t_x = src_y;
                    int t_y = col;
                    
                    int byte_idx = 8 + t_y * 4 + (t_x / 8);
                    int bit_idx = 7 - (t_x % 8);
                    canvas_buf[byte_idx] |= (1 << bit_idx); // Set to White (1)
                }
            }
        }
    }
}

static void draw_luna_to_canvas(const uint8_t *src) {
    int offset_y = TOWER_HEIGHT - 22; // Put Luna at bottom of tower
    for (int page = 0; page < 3; page++) {
        for (int col = 0; col < 32; col++) {
            uint8_t byte_val = src[col + page * 32];
            for (int bit = 0; bit < 8; bit++) {
                int src_y = page * 8 + bit;
                if (src_y >= 22) break;
                
                if (byte_val & (1 << bit)) {
                    int t_x = col;
                    int t_y = offset_y + src_y;
                    
                    int byte_idx = 8 + t_y * 4 + (t_x / 8);
                    int bit_idx = 7 - (t_x % 8);
                    canvas_buf[byte_idx] |= (1 << bit_idx); // Set to White (1)
                }
            }
        }
    }
}

static void rotate_canvas_to_frame(void) {
    // Fill hardware buffer with 1s (Hardware Unlit/Black)
    memset(&frame_buf[8], 0xFF, HBUF_DATA_SIZE);

    for (int t_y = 0; t_y < TOWER_HEIGHT; t_y++) {
        for (int t_x = 0; t_x < TOWER_WIDTH; t_x++) {
            int src_byte = 8 + t_y * 4 + (t_x / 8);
            int src_bit = 7 - (t_x % 8);
            int px_is_white = (canvas_buf[src_byte] >> src_bit) & 1;

            if (px_is_white) {
                // Rotate 90 CCW to map tower to hardware
                int hw_x = t_y;
                int hw_y = 31 - t_x;

                // Alternate rotation if screen is upside down:
                // int hw_x = 127 - t_y;
                // int hw_y = t_x;

                int dst_byte = 8 + hw_y * HBUF_STRIDE + (hw_x / 8);
                int dst_bit = 7 - (hw_x % 8);
                
                // Clear bit to 0 (Hardware Lit/White)
                frame_buf[dst_byte] &= ~(1 << dst_bit);
            }
        }
    }
}

static uint32_t step_counter = 0;
static lv_obj_t *img_widget = NULL;
static lv_obj_t *canvas_widget = NULL;

static const char *get_active_layer_name(void) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint8_t layer = zmk_keymap_highest_layer_active();
    switch (layer) {
        case 0: return "BASE";
        case 1: return "LOW";
        case 2: return "RSE";
        case 3: return "ADJ";
        default: return "LYR";
    }
#else
    return "PER";
#endif
}

static uint8_t get_battery_level(void) {
#if __has_include(<zmk/battery.h>)
    return zmk_battery_state_of_charge();
#else
    return 100;
#endif
}

static void fill_battery_buf(char *bat_buf, uint8_t level) {
    if (level >= 100) {
        bat_buf[0] = '1'; bat_buf[1] = '0'; bat_buf[2] = '0'; bat_buf[3] = '%'; bat_buf[4] = '\0';
    } else {
        bat_buf[0] = (level / 10) + '0'; bat_buf[1] = (level % 10) + '0'; bat_buf[2] = '%'; bat_buf[3] = '\0';
    }
}

static void anim_timer_cb(lv_timer_t *timer) {
    (void)timer;
    step_counter++;

    // 1. Clear the canvas to black
    lv_canvas_fill_bg(canvas_widget, lv_color_black(), LV_OPA_COVER);

    // 2. Draw text using canvas (upright on tower!)
    lv_draw_label_dsc_t label_dsc;
    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_white();
    label_dsc.font = lv_obj_get_style_text_font(img_widget, LV_PART_MAIN);
    label_dsc.align = LV_TEXT_ALIGN_CENTER;

    char bat_buf[6];
    fill_battery_buf(bat_buf, get_battery_level());

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    lv_canvas_draw_text(canvas_widget, 0, 0, TOWER_WIDTH, &label_dsc, get_active_layer_name());
    // Draw battery in the empty space below Bongo Cat (y=112)
    lv_canvas_draw_text(canvas_widget, 0, 112, TOWER_WIDTH, &label_dsc, bat_buf);
#else
    // Luna is at the bottom (y=106), so text goes at the top
    lv_canvas_draw_text(canvas_widget, 0, 0, TOWER_WIDTH, &label_dsc, "PER");
    lv_canvas_draw_text(canvas_widget, 0, 14, TOWER_WIDTH, &label_dsc, bat_buf);
#endif

    // 3. Draw animations onto the upright canvas
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    uint32_t phase = step_counter % 28;
    if (phase < 15) {
        draw_bongo_to_canvas(bongo_tap[phase % 2]);
    } else if (phase < 25) {
        draw_bongo_to_canvas(bongo_idle[(phase - 15) % 5]);
    } else {
        draw_bongo_to_canvas(bongo_prep);
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
    draw_luna_to_canvas(sprite_frame);
#endif

    // 4. Translate 32x128 canvas into 128x32 hardware frame
    rotate_canvas_to_frame();

    // 5. Tell LVGL to repaint the hardware frame
    lv_img_cache_invalidate_src(&frame_dsc);
    if (img_widget != NULL) {
        lv_obj_invalidate(img_widget);
    }
}

static void screen_delete_cb(lv_event_t * e) {
    img_widget = NULL;
    canvas_widget = NULL;
}

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    init_frame_palette();

    // Create a hidden canvas for composing the upright 32x128 tower
    canvas_widget = lv_canvas_create(screen);
    lv_canvas_set_buffer(canvas_widget, canvas_buf, TOWER_WIDTH, TOWER_HEIGHT, LV_IMG_CF_INDEXED_1BIT);
    lv_canvas_set_palette(canvas_widget, 0, lv_color_black());
    lv_canvas_set_palette(canvas_widget, 1, lv_color_white());
    lv_obj_add_flag(canvas_widget, LV_OBJ_FLAG_HIDDEN); // Hidden! It only renders into canvas_buf.

    // Create the image widget that actually talks to the screen hardware (128x32)
    img_widget = lv_img_create(screen);
    lv_img_set_src(img_widget, &frame_dsc);
    lv_obj_align(img_widget, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_add_event_cb(screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

    static lv_timer_t * anim_timer = NULL;
    if (anim_timer == NULL) {
        // Kick off the first frame
        anim_timer_cb(NULL);
        anim_timer = lv_timer_create(anim_timer_cb, 180, NULL);
    }
    
    return screen;
}
