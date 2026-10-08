/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Platform-only LVGL 9.6 smoke application for Gate 1 validation.
 * It intentionally does not include the D50T product UI.
 */

#include <rtconfig.h>
#include <rtthread.h>

#define LOG_TAG "lvgl.aic.smoke"
#define LOG_LVL LOG_LVL_INFO
#include <ulog.h>

#include "lvgl_aic.h"
#include "lv_os_private.h"
#include "lv_aic_display.h"
#include "lv_aic_manual_test.h"
#include "lvgl_aic_build_id.h"
#ifdef AIC_LVGL_SMOKE_WATCHDOG
#include "lv_aic_watchdog.h"
#endif
#ifdef AIC_LVGL_SMOKE_CAN_OTA
#include "update/lv_aic_can_ota.h"
#include "update/lv_aic_can_ota_widget.h"
#endif

typedef struct {
    lv_thread_sync_t *sync;
    rt_sem_t ready;
    rt_sem_t done;
    lv_result_t result;
} lvgl_aic_sync_waiter_t;

static void lvgl_aic_sync_waiter(void *parameter)
{
    lvgl_aic_sync_waiter_t *ctx = parameter;
    rt_sem_release(ctx->ready);
    ctx->result = lv_thread_sync_wait(ctx->sync);
    rt_sem_release(ctx->done);
    /* 由测试线程删除仍存活的工作线程，不使用已退出线程的句柄。 */
    for (;;) rt_thread_mdelay(1000);
}

static int lvgl_aic_sync_selftest(void)
{
    lv_thread_sync_t sync = {0};
    rt_uint32_t received = 0;
    rt_thread_t thread = RT_NULL;
    lvgl_aic_sync_waiter_t waiter = {0};
    int result = -1;
    if (lv_thread_sync_init(&sync) != LV_RESULT_OK) return -1;
    /* 等待前的重复通知合并为一个；消费之后不能留下计数积压。 */
    if (lv_thread_sync_signal(&sync) != LV_RESULT_OK ||
        lv_thread_sync_signal(&sync) != LV_RESULT_OK ||
        lv_thread_sync_wait(&sync) != LV_RESULT_OK) goto out;
    if (rt_event_recv(sync.event, 1U, RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_NO, &received) != -RT_ETIMEOUT) goto out;
    if (lv_thread_sync_signal_isr(&sync) != LV_RESULT_OK ||
        lv_thread_sync_wait(&sync) != LV_RESULT_OK) goto out;
    waiter.sync = &sync;
    waiter.result = LV_RESULT_INVALID;
    waiter.ready = rt_sem_create("lv_ready", 0, RT_IPC_FLAG_PRIO);
    waiter.done = rt_sem_create("lv_done", 0, RT_IPC_FLAG_PRIO);
    if (!waiter.ready || !waiter.done) goto out;
    thread = rt_thread_create("lv_wait", lvgl_aic_sync_waiter, &waiter, 1024, 30, 10);
    if (!thread || rt_thread_startup(thread) != RT_EOK) goto out;
    if (rt_sem_take(waiter.ready, 100) != RT_EOK ||
        lv_thread_sync_signal(&sync) != LV_RESULT_OK ||
        rt_sem_take(waiter.done, 100) != RT_EOK || waiter.result != LV_RESULT_OK) goto out;
    result = 0;
out:
    if (thread) rt_thread_delete(thread);
    if (waiter.ready) rt_sem_delete(waiter.ready);
    if (waiter.done) rt_sem_delete(waiter.done);
    if (lv_thread_sync_delete(&sync) != LV_RESULT_OK) result = -1;
    return result;
}

static void lvgl_aic_smoke_delay(uint32_t delay_ms)
{
    if (delay_ms == LV_NO_TIMER_READY) {
        delay_ms = LV_DEF_REFR_PERIOD;
    }
    if (delay_ms == 0U) {
        rt_thread_yield();
    } else {
        rt_thread_mdelay(delay_ms);
    }
}

static int lvgl_aic_smoke_cycle(uint32_t frame_count)
{
    int result;

    result = lv_aic_init();
    if (result != LV_AIC_OK) {
        LOG_E("lv_aic_init failed: %d", result);
        return result;
    }

    result = lv_aic_manual_test_create();
    if (result != LV_AIC_OK) {
        LOG_E("manual test page creation failed: %d", result);
        lv_aic_manual_test_deinit();
        lv_aic_deinit();
        return result;
    }

    lv_aic_display_flush_count_reset();
    lv_refr_now(lv_display_get_default());
    for (uint32_t i = 0; i < frame_count; ++i) {
        uint32_t delay_ms = lv_timer_handler();
        if ((delay_ms != LV_NO_TIMER_READY) && (delay_ms > 0U) && (delay_ms < 1000U)) {
            rt_thread_mdelay(delay_ms);
        }
    }
    if (lv_aic_display_flush_count_get() == 0U) {
        LOG_E("lifecycle cycle completed without a presented frame");
        lv_aic_manual_test_deinit();
        lv_aic_deinit();
        return LV_AIC_ERR_INVALID_STATE;
    }

    lv_aic_manual_test_deinit();
    lv_aic_deinit();
    return LV_AIC_OK;
}

static void lvgl_aic_smoke_thread(void *parameter)
{
    int result;

    (void)parameter;
    /* One identity per line: ulog formats each record into a 128-byte buffer
     * that also carries the timestamp, level/tag and colour codes, so the
     * combined banner lost its lvgl tail on the board console. */
    LOG_I("build: %s", AIC_LVGL_BUILD_SDK);
    LOG_I("build: %s", AIC_LVGL_BUILD_AIC);
    LOG_I("build: %s", AIC_LVGL_BUILD_LVGL);
    if (lvgl_aic_sync_selftest() != 0) {
        LOG_E("RT-Thread event binary-sync self-test failed");
        return;
    }
    LOG_I("RT-Thread event binary-sync self-test passed");

    lv_init();

    for (int cycle = 0; cycle < AIC_LVGL_SMOKE_CYCLES; ++cycle) {
        result = lvgl_aic_smoke_cycle(AIC_LVGL_SMOKE_FRAMES);
        if (result != LV_AIC_OK) {
            LOG_E("lifecycle cycle %d failed", cycle + 1);
            return;
        }
        LOG_I("lifecycle cycle %d passed", cycle + 1);
    }

    result = lv_aic_init();
    if (result != LV_AIC_OK) {
        LOG_E("final lv_aic_init failed: %d", result);
        return;
    }

#if AIC_LVGL_USE_MPP_DEC
    if (lv_aic_mpp_test_run() != 0) {
        LOG_E("MPP tests failed; inspect logs, page remains available");
    }
#endif

    result = lv_aic_manual_test_create();
    if (result != LV_AIC_OK) {
        LOG_E("final manual test page creation failed: %d", result);
        lv_aic_manual_test_deinit();
        lv_aic_deinit();
        return;
    }

#if AIC_LVGL_USE_GE2D
    /* Measure one forced full refresh of the page. Runs before the flush-count
     * reset below so its own frame does not satisfy the SW baseline check. */
    if (lv_aic_ge2d_test_run() != 0) {
        LOG_E("GE2D checks failed; page remains available for inspection");
    }
#endif

    lv_aic_display_flush_count_reset();
    lv_refr_now(lv_display_get_default());
    LOG_I("LVGL 9.6 smoke page is running");
    {
        bool frame_logged = false;
        for (;;) {
            uint32_t delay_ms = lv_timer_handler();
#ifdef AIC_LVGL_SMOKE_CAN_OTA
            /* Show/close the OTA progress overlay on this (LVGL) thread. */
            lv_aic_can_ota_ui_poll();
#endif
            if (!frame_logged && (lv_aic_display_flush_count_get() > 0U)) {
                LOG_I("first frame presented; scheduler is still progressing");
                frame_logged = true;
#ifdef AIC_LVGL_SMOKE_CAN_OTA_AUTOSTART
                /* Health gate for a trial boot: the host confirms only an
                 * image that reached this point and answers over CAN. */
                LOG_I("CAN OTA endpoint autostart: %s",
                      lv_aic_can_ota_start() ? "ok" : "failed");
#endif
            }
            lvgl_aic_smoke_delay(delay_ms);
        }
    }
}

int main(void)
{
    rt_thread_t thread;

    ulog_global_filter_lvl_set(ULOG_OUTPUT_LVL);

#ifdef AIC_LVGL_SMOKE_WATCHDOG
    if (!lv_aic_watchdog_start()) {
        LOG_E("watchdog start failed; bench lockups will not reboot");
    }
#endif

    thread = rt_thread_create("lvgl_aic_smoke",
                              lvgl_aic_smoke_thread,
                              RT_NULL,
                              AIC_LVGL_SMOKE_THREAD_STACK_SIZE,
                              AIC_LVGL_SMOKE_THREAD_PRIO,
                              10);
    if (thread == RT_NULL) {
        LOG_E("failed to create LVGL smoke thread");
        return -1;
    }

    if (rt_thread_startup(thread) != RT_EOK) {
        LOG_E("failed to start LVGL smoke thread");
        rt_thread_delete(thread);
        return -1;
    }

    return 0;
}
