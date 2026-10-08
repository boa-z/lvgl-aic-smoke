/* SPDX-License-Identifier: Apache-2.0 */
#ifndef LV_AIC_WATCHDOG_H
#define LV_AIC_WATCHDOG_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bench recovery watchdog of the smoke application: a low-priority thread
 * feeds the "wdt" device while the scheduler runs, so a lockup that stops the
 * scheduler (CPU exception, hard loop with interrupts off) resets the board
 * into the shell instead of leaving the bench hung. Long legitimate work
 * (GE2D suites, CAN streaming) yields the scheduler and keeps feeding.
 * Not part of lvgl-aic: a product has its own watchdog supervision. */
bool lv_aic_watchdog_start(void);
void lv_aic_watchdog_stop(void);
bool lv_aic_watchdog_running(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_AIC_WATCHDOG_H */
