/* SPDX-License-Identifier: Apache-2.0 */
/* OTA progress overlay for the smoke app. Layout mirrors forklift's
 * meter_update_widget (column flex, live progress bar); texts are fixed
 * English and the view comes from lv_aic_can_ota_view(). */
#include "lvgl_aic.h"
#include "update/lv_aic_can_ota_widget.h"
#ifdef AIC_LVGL_SMOKE_CAN_OTA
#include "update/lv_aic_can_ota.h"
#include "update/meter_update.h"
#include "update/lv_aic_can_ota.h"

#include <stdio.h>
#include <string.h>

#if LV_FONT_MONTSERRAT_14
#define OTA_FONT (&lv_font_montserrat_14)
#else
#define OTA_FONT (LV_FONT_DEFAULT)
#endif

static struct {
    lv_obj_t *root, *title, *phase, *bar, *percent, *versions, *error, *note;
    lv_timer_t *refresh;
    volatile bool pending_show, pending_close;
    bool ready;
} w;

static void ota_text_set(lv_obj_t *label, const char *text)
{
    const char *value = text ? text : "";
    if (strcmp(lv_label_get_text(label), value)) {
        lv_label_set_text(label, value);
    }
}

static lv_obj_t *ota_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(94));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, OTA_FONT, 0);
    lv_label_set_text(label, "");
    return label;
}

static void ota_refresh_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_aic_can_ota_widget_refresh();
}

lv_obj_t *lv_aic_can_ota_widget_create(void)
{
    if (w.root != NULL) {
        return w.root;
    }
    memset(&w, 0, sizeof(w));
    w.root = lv_obj_create(lv_layer_top());
    if (w.root == NULL) {
        return NULL;
    }
    lv_obj_set_size(w.root, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(w.root, 0, 0);
    lv_obj_set_style_bg_color(w.root, lv_color_hex(0x091a25), 0);
    lv_obj_set_style_bg_opa(w.root, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w.root, 0, 0);
    lv_obj_set_style_border_width(w.root, 0, 0);
    lv_obj_set_style_text_color(w.root, lv_color_hex(0xe9f2f5), 0);
    lv_obj_set_style_pad_all(w.root, 28, 0);
    lv_obj_set_style_pad_row(w.root, 18, 0);
    lv_obj_set_scrollable(w.root, false);
    lv_obj_set_hidden(w.root, true);
    lv_obj_set_flex_flow(w.root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(w.root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    w.title = ota_label(w.root);
    w.phase = ota_label(w.root);
    w.bar = lv_bar_create(w.root);
    lv_obj_set_size(w.bar, lv_pct(82), 18);
    lv_bar_set_range(w.bar, 0, 100);
    lv_obj_set_style_bg_color(w.bar, lv_color_hex(0x244050), LV_PART_MAIN);
    lv_obj_set_style_bg_color(w.bar, lv_color_hex(0x5de5ca), LV_PART_INDICATOR);
    w.percent = ota_label(w.root);
    w.versions = ota_label(w.root);
    w.error = ota_label(w.root);
    lv_obj_set_style_text_color(w.error, lv_color_hex(0xff7979), 0);
    w.note = ota_label(w.root);
    lv_obj_set_style_text_color(w.note, lv_color_hex(0x9cb5c4), 0);
    w.refresh = lv_timer_create(ota_refresh_cb, 500, NULL);
    if (w.refresh == NULL) {
        lv_aic_can_ota_widget_destroy();
        return NULL;
    }
    lv_aic_can_ota_widget_refresh();
    return w.root;
}

void lv_aic_can_ota_widget_destroy(void)
{
    if (w.refresh != NULL) {
        lv_timer_delete(w.refresh);
        w.refresh = NULL;
    }
    if (w.root != NULL) {
        lv_obj_delete(w.root);
    }
    memset(&w, 0, sizeof(w));
}

void lv_aic_can_ota_widget_present_view(const meter_update_view_t *v)
{
    char text[256];
    uint32_t percent;
    if (w.root == NULL || v == NULL) {
        return;
    }
    if (!v->visible) {
        lv_obj_set_hidden(w.root, true);
        return;
    }
    lv_obj_set_hidden(w.root, false);
    ota_text_set(w.title, "Firmware update (CAN-USB)");
    ota_text_set(w.phase, meter_update_state_name(v->state));
    percent = v->total ? (uint32_t)((uint64_t)v->received * 100u / v->total) : 0u;
    if (percent > 100u) {
        percent = 100u;
    }
    if (lv_bar_get_value(w.bar) != (int32_t)percent) {
        lv_bar_set_value(w.bar, (int32_t)percent, LV_ANIM_OFF);
    }
    snprintf(text, sizeof(text), "%u%%   %u / %u B", (unsigned)percent,
             (unsigned)v->received, (unsigned)v->total);
    ota_text_set(w.percent, text);
    snprintf(text, sizeof(text), "Current: %.47s\nTarget: %.47s",
             v->current_version, v->target_version);
    ota_text_set(w.versions, text);
    if (v->error) {
        snprintf(text, sizeof(text), "Error: %u", (unsigned)v->error);
        ota_text_set(w.error, text);
        lv_obj_set_hidden(w.error, false);
    } else {
        lv_obj_set_hidden(w.error, true);
    }
    ota_text_set(w.note, "PCAN 1M ID 7E0/7E8; ota_can maintenance on to admit");
    {
        lv_color_t color = lv_color_hex(v->state == METER_UPDATE_FAILED ? 0xff7979 : 0x5de5ca);
        if (lv_color_to_u32(lv_obj_get_style_text_color(w.phase, 0)) != lv_color_to_u32(color)) {
            lv_obj_set_style_text_color(w.phase, color, 0);
        }
    }
}

void lv_aic_can_ota_widget_refresh(void)
{
    meter_update_view_t view;
    lv_aic_can_ota_view(&view);
    lv_aic_can_ota_widget_present_view(&view);
}

#if AIC_LVGL_BSP_RTTHREAD
#include <rtthread.h>
#include <rthw.h>

void lv_aic_can_ota_ui_poll(void)
{
    bool show = false, close = false;
    rt_base_t level = rt_hw_interrupt_disable();
    w.ready = true;
    if (w.pending_show) {
        w.pending_show = false;
        show = true;
    }
    if (w.pending_close) {
        w.pending_close = false;
        close = true;
    }
    rt_hw_interrupt_enable(level);
    if (close) {
        lv_aic_can_ota_widget_destroy();
    } else if (show) {
        lv_aic_can_ota_widget_create();
    }
}

void lv_aic_can_ota_ui_request(bool show)
{
    rt_base_t level = rt_hw_interrupt_disable();
    if (w.ready) {
        if (show) {
            w.pending_show = true;
        } else {
            w.pending_close = true;
        }
    }
    rt_hw_interrupt_enable(level);
}
#else
void lv_aic_can_ota_ui_poll(void) {}
#endif
#endif
