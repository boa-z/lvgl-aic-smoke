/* SPDX-License-Identifier: Apache-2.0 */
/* Time-based watchdog feeding for bench recovery. This catches scheduler
 * death only; a task-level deadlock with the scheduler alive keeps feeding. */
#include <rtconfig.h> /* the Kconfig switch must be visible to the #ifdef below */
#include "lv_aic_watchdog.h"
#ifdef AIC_LVGL_SMOKE_WATCHDOG

#include <rtthread.h>
#include <rthw.h>
#include <rtdevice.h>
#include <finsh.h>
#include <string.h>

#ifndef AIC_LVGL_SMOKE_WATCHDOG_TIMEOUT_S
#define AIC_LVGL_SMOKE_WATCHDOG_TIMEOUT_S 10
#endif
#ifndef AIC_LVGL_SMOKE_WATCHDOG_FEED_MS
#define AIC_LVGL_SMOKE_WATCHDOG_FEED_MS 3000
#endif

static rt_device_t wdt;
static rt_thread_t feed_thread;
static volatile bool running;
static volatile bool stop_requested;

static void feed_entry(void *parameter)
{
    (void)parameter;
    while (!stop_requested) {
        rt_device_control(wdt, RT_DEVICE_CTRL_WDT_KEEPALIVE, RT_NULL);
        rt_thread_mdelay(AIC_LVGL_SMOKE_WATCHDOG_FEED_MS);
    }
    /* Disarm the hardware as well: an unfed armed watchdog would reset the
     * board shortly after "stop" (debugger halts, intentional pauses). */
    rt_device_control(wdt, RT_DEVICE_CTRL_WDT_STOP, RT_NULL);
    running = false;
}

bool lv_aic_watchdog_start(void)
{
    uint16_t timeout = AIC_LVGL_SMOKE_WATCHDOG_TIMEOUT_S;
    if (running) {
        return true;
    }
    stop_requested = false;
    wdt = rt_device_find("wdt");
    if (wdt == RT_NULL) {
        return false;
    }
    /* drv_wdt_init() enables the module clock and releases the block from
     * reset. Without it every write to the control register is ignored: the
     * thresholds take, START reports success, and the counter never runs (the
     * first version of this file never armed the hardware). */
    if (rt_device_init(wdt) != RT_EOK) {
        return false;
    }
    if (rt_device_control(wdt, RT_DEVICE_CTRL_WDT_SET_TIMEOUT, &timeout) != RT_EOK ||
        rt_device_control(wdt, RT_DEVICE_CTRL_WDT_START, RT_NULL) != RT_EOK) {
        return false;
    }
    feed_thread = rt_thread_create("wdt_feed", feed_entry, RT_NULL, 1024, 29, 10);
    if (feed_thread == RT_NULL || rt_thread_startup(feed_thread) != RT_EOK) {
        rt_device_control(wdt, RT_DEVICE_CTRL_WDT_STOP, RT_NULL);
        return false;
    }
    running = true;
    return true;
}

void lv_aic_watchdog_stop(void)
{
    stop_requested = true;
}

bool lv_aic_watchdog_running(void)
{
    return running;
}

static int lv_aic_watchdog(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "status")) {
        uint16_t left = 0;
        if (wdt == RT_NULL ||
            rt_device_control(wdt, RT_DEVICE_CTRL_WDT_GET_TIMELEFT, &left) != RT_EOK) {
            rt_kprintf("watchdog unavailable\n");
            return -1;
        }
        rt_kprintf("watchdog running=%d timeleft=%us\n", running, left);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "stop")) {
        lv_aic_watchdog_stop();
        rt_kprintf("watchdog stopping (no reboot until next start)\n");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "start")) {
        rt_kprintf(lv_aic_watchdog_start() ? "watchdog started\n" : "watchdog start failed\n");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "regs")) {
        /* Raw hardware view (D13x), read-only: shows whether the counter
         * actually runs and which thresholds are programmed. */
#ifndef WDT_BASE
#define WDT_BASE 0x19000000UL
#endif
#define WDT_R(off) (*(volatile unsigned int *)(WDT_BASE + (off)))
        unsigned int cnt_a = WDT_R(0x004), cnt_b;
        rt_thread_mdelay(1000);
        cnt_b = WDT_R(0x004);
        rt_kprintf("wdt ctl=0x%08x en=%u cnt=%u -> %u (+%u ticks in 1 s, 32000 = counting)\n",
                   WDT_R(0x000), WDT_R(0x000) & 1u, cnt_a, cnt_b, cnt_b - cnt_a);
        rt_kprintf("wdt irq_en=0x%x irq_sta=0x%x clr_thd=%us irq_thd=%us rst_thd=%us rst_sel=0x%x\n",
                   WDT_R(0x008), WDT_R(0x00C), WDT_R(0x040) / 32000u, WDT_R(0x044) / 32000u,
                   WDT_R(0x048) / 32000u, WDT_R(0x020));
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "hang")) {
        /* Self-test of the recovery path: really lock the CPU. */
        if (!running) {
            rt_kprintf("watchdog is not running; refusing to hang the board\n");
            return -1;
        }
        rt_kprintf("watchdog self-test: hanging with interrupts off; the board must reset "
                   "within ~%d s\n", AIC_LVGL_SMOKE_WATCHDOG_TIMEOUT_S);
        rt_thread_mdelay(300); /* let the console drain before interrupts go off */
        rt_hw_interrupt_disable();
        for (;;) {
        }
    }
    rt_kprintf("lv_aic_watchdog start|stop|status|regs|hang\n");
    return -1;
}
MSH_CMD_EXPORT(lv_aic_watchdog, Bench recovery watchdog controls);

#else /* AIC_LVGL_SMOKE_WATCHDOG */

bool lv_aic_watchdog_start(void) { return false; }
void lv_aic_watchdog_stop(void) {}
bool lv_aic_watchdog_running(void) { return false; }

#endif
