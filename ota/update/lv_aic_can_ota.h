/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LV_AIC_CAN_OTA_H
#define LV_AIC_CAN_OTA_H

#include <stdbool.h>
#include "contracts/meter_update_view.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UDS endpoint on CAN0: RX 0x7E0, TX 0x7E8, classic CAN at the product
 * 500 kbit/s convention (same wiring as forklift-meter-platform). */
#define LV_AIC_CAN_OTA_RX_ID 0x7E0u
#define LV_AIC_CAN_OTA_TX_ID 0x7E8u

bool lv_aic_can_ota_start(void);
void lv_aic_can_ota_stop(void);
bool lv_aic_can_ota_started(void);
bool lv_aic_can_ota_stopped(void);
/* Snapshot of the endpoint for UI display; safe from any thread. */
void lv_aic_can_ota_view(meter_update_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* LV_AIC_CAN_OTA_H */
