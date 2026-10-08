/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LV_AIC_CAN_OTA_WIDGET_H
#define LV_AIC_CAN_OTA_WIDGET_H

#include "contracts/meter_update_view.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fullscreen top-layer overlay with title/phase/bar/percent/versions/
 * error/note, mirroring forklift's meter_update_widget (fixed English
 * texts; smoke has no translation pack). All calls run on the UI thread:
 * create/show starts a 500 ms refresh timer that presents the live view.
 * present_view() takes an explicit view so host contracts can drive it. */
lv_obj_t *lv_aic_can_ota_widget_create(void);
void lv_aic_can_ota_widget_destroy(void);
void lv_aic_can_ota_widget_present_view(const meter_update_view_t *view);
void lv_aic_can_ota_widget_refresh(void);
/* Runs on the LVGL owner thread (the smoke app's main loop). */
void lv_aic_can_ota_ui_poll(void);
/* Queue an overlay show (true) or close (false) for the UI thread. */
void lv_aic_can_ota_ui_request(bool show);

#ifdef __cplusplus
}
#endif

#endif /* LV_AIC_CAN_OTA_WIDGET_H */
