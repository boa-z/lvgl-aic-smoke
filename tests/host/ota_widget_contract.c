/* SPDX-License-Identifier: Apache-2.0 */
/* Host contract for the OTA progress overlay. Synthetic views drive
 * present_view(); no endpoint, CAN or backend is involved. */
#include "lvgl.h"
#include "update/lv_aic_can_ota_widget.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t pixels[800 * 480];
static unsigned flushes;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    (void)area;
    (void)data;
    flushes++;
    lv_display_flush_ready(display);
}

static void make_view(meter_update_view_t *v, bool visible,
                      meter_update_state_t state, uint32_t received,
                      uint32_t total, uint32_t error)
{
    memset(v, 0, sizeof(*v));
    v->visible = visible;
    v->state = state;
    v->received = received;
    v->total = total;
    v->error = error;
    snprintf(v->current_version, sizeof(v->current_version), "1.0.0");
    snprintf(v->target_version, sizeof(v->target_version), "1.0.1");
}

static void run_frames(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

/* Stub: the endpoint port object is target-only; the contract drives
 * synthetic views through present_view() instead. */
void lv_aic_can_ota_view(meter_update_view_t *view)
{
    memset(view, 0, sizeof(*view));
}

int main(void)
{
    meter_update_view_t v;
    lv_obj_t *root;
    bool varied = false;
    size_t i;

    lv_init();
    {
        lv_display_t *display = lv_display_create(800, 480);
        assert(display);
        lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
        lv_display_set_buffers(display, pixels, NULL, sizeof(pixels),
                               LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(display, flush);
    }

    root = lv_aic_can_ota_widget_create();
    assert(root != NULL);
    assert(lv_obj_get_child_count(root) == 7);
    run_frames(2);
    /* Endpoint not started: live view is invisible, overlay stays hidden. */
    assert(lv_obj_is_hidden(root));

    make_view(&v, true, METER_UPDATE_DOWNLOADING, 512, 1024, 0);
    lv_aic_can_ota_widget_present_view(&v);
    /* One frame only: the 500 ms live refresh would re-hide the overlay
     * on its next tick (stub view is invisible). */
    lv_tick_inc(100);
    lv_timer_handler();
    assert(!lv_obj_is_hidden(root));
    assert(lv_bar_get_value(lv_obj_get_child(root, 2)) == 50);
    assert(flushes > 0);
    for (i = 1; i < 800u * 480u; i++) {
        if (pixels[i] != pixels[0]) {
            varied = true;
            break;
        }
    }
    assert(varied);

    make_view(&v, true, METER_UPDATE_FAILED, 100, 1024, 3);
    lv_aic_can_ota_widget_present_view(&v);
    assert(lv_bar_get_value(lv_obj_get_child(root, 2)) == 9);
    assert(!lv_obj_is_hidden(lv_obj_get_child(root, 5)));

    make_view(&v, false, METER_UPDATE_IDLE, 0, 0, 0);
    lv_aic_can_ota_widget_present_view(&v);
    assert(lv_obj_is_hidden(root));

    lv_aic_can_ota_widget_destroy();
    run_frames(4);
    lv_deinit();
    puts("PASS ota widget lifecycle/progress/render (software, virtual time)");
    return 0;
}
