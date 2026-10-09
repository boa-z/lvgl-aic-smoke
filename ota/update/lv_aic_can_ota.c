/* SPDX-License-Identifier: Apache-2.0 */
/* Smoke-app CAN-USB OTA endpoint. Adapted from forklift-meter-platform
 * platform/rtthread/meter_update_port.c: same UDS services, job queue and
 * worker, but admission is a shell maintenance flag (no product domain),
 * the NVM durable barrier is trivially true (no app settings store), and
 * CAN I/O goes straight through rt_device can0. Threads are created once
 * at first start and park on stop; no thread is ever deleted. */
#include "lvgl_aic.h"
#include "update/lv_aic_can_ota.h"
#ifdef AIC_LVGL_SMOKE_CAN_OTA
#include "protocols/uds/meter_uds.h"
#include "tp/isotp_c.h"
#include "update/lv_aic_can_ota_widget.h"
#include "update/meter_update_backend.h"
#include <rtdevice.h>
#if defined(AIC_LVGL_USE_CAN_CAPTURE) && AIC_LVGL_USE_CAN_CAPTURE
#include "tests/manual/lv_aic_can_capture.h"
#endif

#if AIC_LVGL_BSP_RTTHREAD
#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>
#include <string.h>

#define OTA_RX_ID LV_AIC_CAN_OTA_RX_ID
#define OTA_TX_ID LV_AIC_CAN_OTA_TX_ID
#define OTA_PRODUCT "lvgl-aic-smoke"
#define OTA_HARDWARE "d50t-2-lite"
/* Firmware identity reported over UDS; build B with a distinct Kconfig
 * version so the host can prove which image is running after reboot. */
#ifdef AIC_LVGL_SMOKE_CAN_OTA_VERSION
#define OTA_VERSION AIC_LVGL_SMOKE_CAN_OTA_VERSION
#else
#define OTA_VERSION "1.0.0"
#endif
#define OTA_TIMEOUT_MS 120000u
/* Bounded in-place wait for the submitted job before falling back to the
 * vendor RCRRP (0x78) path; host client P2 is 2 s. */
#define OTA_RESULT_WAIT_MS 1500u

typedef struct {
    meter_update_error_t error;
    uint32_t generation, offset;
} ota_completion_t;

#define OTA_POOL(name, type, count) \
    static rt_ubase_t name[((sizeof(type) + sizeof(void *) + sizeof(rt_ubase_t) - 1u) / sizeof(rt_ubase_t)) * (count)]

static struct rt_messagequeue ota_jobs, ota_results;
OTA_POOL(ota_job_pool, meter_update_job_t, 1);
OTA_POOL(ota_result_pool, ota_completion_t, 1);

static struct rt_thread ota_rx_thread;
static struct rt_thread ota_svc_thread;
static struct rt_thread ota_worker_thread;
static struct rt_mutex ota_lock;
/* isotp-c is not thread-safe: the RX thread feeds frames while the service
 * thread polls/sends on the same link. This leaf mutex serializes both;
 * it never nests inside ota_lock (service handlers take ota_lock while
 * holding this one — the only nesting direction). */
static struct rt_mutex ota_isotp_lock;
static struct rt_semaphore ota_rx_sem;
/* Signaled after every worker completion; lets result() wait in-place for
 * the just-submitted job instead of exposing the RCRRP re-evaluation
 * window (every RCRRP re-eval re-enters the vendor service handler). */
static struct rt_semaphore ota_result_sem;
static rt_device_t ota_can;
static meter_aic_update_t ota_backend_state;
static meter_firmware_update_t ota_service;
static meter_uds_t ota_uds;
static UDSTpISOTpC_t ota_transport;

typedef struct {
    bool started, maintenance, session_maintenance, admitted, stopping, stopped, threads;
    bool busy, cancel;
    meter_update_state_t state;
    meter_update_error_t error;
    uint32_t generation, received, total, rx, tx, drops;
    char target[METER_UPDATE_VERSION_SIZE];
} ota_shared_t;
static ota_shared_t ota_shared;

static uint32_t ota_now_ms(void)
{
    /* 64-bit intermediate: tick*1000 overflows u32 beyond ~71.6 min of
     * accumulated ticks, producing a non-monotonic sawtooth that breaks
     * ISO-TP/iso14229 timeout comparisons (spurious S3 session timeouts
     * and counter resets mid-transfer). */
    return (uint32_t)((uint64_t)rt_tick_get() * 1000u / RT_TICK_PER_SECOND);
}

/* Thread stacks LAST in the translation unit (same rationale as the
 * reference port): RT-Thread stacks grow downwards out of their static
 * arrays, so an overflow must not land in the service/backend structures.
 * The worker stack also carries the vendor OTA/flash call chain. */
static rt_ubase_t ota_rx_stack[4096 / sizeof(rt_ubase_t)];
static rt_ubase_t ota_svc_stack[8192 / sizeof(rt_ubase_t)];
static rt_ubase_t ota_worker_stack[16384 / sizeof(rt_ubase_t)];

uint32_t UDSMillis(void)
{
    return ota_now_ms();
}

uint32_t isotp_user_get_us(void)
{
    /* µs value must also survive beyond the 71.6 min u32-ms period. */
    return (uint32_t)((uint64_t)ota_now_ms() * 1000u);
}

void isotp_user_debug(const char *message, ...)
{
    (void)message;
}

int isotp_user_send_can(uint32_t id, const uint8_t *data, uint8_t size)
{
    struct rt_can_msg msg;
    /* Runs under the isotp lock (called from poll/send paths); the CAN
     * write itself is driver-serialized, so no extra locking here. */
    if (size > 8u || id != OTA_TX_ID || ota_can == RT_NULL) {
        return ISOTP_RET_ERROR;
    }
    memset(&msg, 0, sizeof(msg));
    msg.id = id;
    msg.ide = RT_CAN_STDID;
    msg.rtr = RT_CAN_DTR;
    msg.len = size;
    msg.hdr = -1;
    memcpy(msg.data, data, size);
    /* The TX mailbox holds a single frame (RT_CANSND_BOX_NUM=1); a busy
     * controller must not drop a response. Bounded retry. */
    for (unsigned attempt = 0; attempt < 8u; attempt++) {
        if (rt_device_write(ota_can, 0, &msg, sizeof(msg)) == sizeof(msg)) {
            rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
            ota_shared.tx++;
            rt_mutex_release(&ota_lock);
            return ISOTP_RET_OK;
        }
        rt_thread_mdelay(2);
    }
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    ota_shared.drops++;
    rt_mutex_release(&ota_lock);
    return ISOTP_RET_NOSPACE;
}

static void ota_publish(void)
{
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    ota_shared.state = ota_service.state;
    ota_shared.error = ota_service.error;
    ota_shared.generation = ota_service.generation;
    ota_shared.received = ota_service.received;
    ota_shared.total = ota_service.manifest.size;
    memcpy(ota_shared.target, ota_service.manifest.version, sizeof(ota_shared.target));
    rt_mutex_release(&ota_lock);
}

static meter_update_error_t ota_confirm(void)
{
    int r = meter_aic_update_confirm();
    rt_kprintf("ota confirm: result=%d\n", r);
    return r == 0 ? METER_UPDATE_OK : r == -2 ? METER_UPDATE_STATE : METER_UPDATE_BACKEND;
}

static void ota_worker_entry(void *arg)
{
    meter_update_job_t job;
    (void)arg;
    for (;;) {
        bool got = rt_mq_recv(&ota_jobs, &job, sizeof(job),
                              rt_tick_from_millisecond(20)) == RT_EOK;
        rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
        bool cancel = ota_shared.cancel, admitted = ota_shared.admitted;
        bool stopping = ota_shared.stopping;
        rt_mutex_release(&ota_lock);
        if (stopping) {
            (void)meter_update_abort(&ota_service, ota_service.generation);
            ota_publish();
            rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
            ota_shared.stopped = true;
            rt_mutex_release(&ota_lock);
            return;
        }
        if (cancel) {
            ota_completion_t discarded;
            while (rt_mq_recv(&ota_results, &discarded, sizeof(discarded), 0) == RT_EOK) {
            }
            rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
            while (rt_mq_recv(&ota_jobs, &job, sizeof(job), 0) == RT_EOK) {
            }
            ota_shared.busy = false;
            ota_shared.cancel = false;
            rt_mutex_release(&ota_lock);
            ota_publish();
            continue;
        }
        meter_update_tick(&ota_service, ota_now_ms());
        if (got) {
            meter_update_error_t e = METER_UPDATE_STATE;
            switch (job.kind) {
            case METER_UPDATE_JOB_BEGIN:
                e = meter_update_begin(&ota_service, &job.manifest, admitted, ota_now_ms());
                break;
            case METER_UPDATE_JOB_WRITE:
                e = meter_update_write(&ota_service, job.generation, job.offset,
                                       job.data, job.size, ota_now_ms());
                break;
            case METER_UPDATE_JOB_VERIFY:
                e = meter_update_verify(&ota_service, job.generation, ota_now_ms());
                break;
            case METER_UPDATE_JOB_ABORT:
                e = meter_update_abort(&ota_service, job.generation);
                break;
            case METER_UPDATE_JOB_ACTIVATE:
                /* No NVM settings store in smoke: the barrier is trivially met. */
                e = meter_update_activate(&ota_service, job.generation, admitted);
                break;
            case METER_UPDATE_JOB_CONFIRM:
                /* Trial-boot confirmation: the running image answered over
                 * CAN, which is the health gate. Not a trial -> sequence. */
                e = ota_confirm();
                break;
            }
            {
                ota_completion_t result = {e, ota_service.generation, ota_service.received};
                ota_publish();
                rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
                if (!ota_shared.cancel &&
                    rt_mq_send(&ota_results, &result, sizeof(result)) != RT_EOK) {
                    ota_shared.cancel = true;
                }
                rt_mutex_release(&ota_lock);
                rt_sem_release(&ota_result_sem);
            }
        } else {
            ota_publish();
        }
    }
}

static bool ota_submit(void *ctx, const meter_update_job_t *job)
{
    (void)ctx;
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    bool accepted = !ota_shared.stopping && !ota_shared.cancel && !ota_shared.busy &&
                    rt_mq_send(&ota_jobs, job, sizeof(*job)) == RT_EOK;
    if (accepted) {
        ota_shared.busy = true;
    }
    rt_mutex_release(&ota_lock);
    return accepted;
}

static bool ota_result(void *ctx, meter_update_error_t *error, uint32_t *gen, uint32_t *offset)
{
    (void)ctx;
    ota_completion_t r;
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    bool have = !ota_shared.cancel && rt_mq_recv(&ota_results, &r, sizeof(r), 0) == RT_EOK;
    rt_mutex_release(&ota_lock);
    if (!have) {
        if (ota_shared.cancel) {
            return false;
        }
        /* Wait in place (bounded) for the submitted job so the first
         * evaluation can answer directly; the RCRRP re-evaluation window
         * re-enters the vendor service handler and must stay rare. No
         * reset first: a stale pulse (completion whose result the MQ
         * already delivered) just makes this take return early, and the
         * MQ check below still decides. */
        (void)rt_sem_take(&ota_result_sem,
                          rt_tick_from_millisecond(OTA_RESULT_WAIT_MS));
        rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
        have = !ota_shared.cancel && rt_mq_recv(&ota_results, &r, sizeof(r), 0) == RT_EOK;
        rt_mutex_release(&ota_lock);
        if (!have) {
            return false;
        }
    }
    *error = r.error;
    *gen = r.generation;
    *offset = r.offset;
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    ota_shared.busy = false;
    rt_mutex_release(&ota_lock);
    return true;
}

static size_t ota_info(void *ctx, uint8_t *out, size_t capacity)
{
    (void)ctx;
    ota_shared_t snapshot;
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    snapshot = ota_shared;
    snapshot.maintenance = ota_shared.maintenance || ota_shared.session_maintenance;
    rt_mutex_release(&ota_lock);
    meter_aic_boot_t boot = meter_aic_update_boot();
    int n = snprintf((char *)out, capacity,
                     "{\"product\":\"%s\",\"hardware\":\"%s\",\"version\":\"%s\","
                     "\"slot\":\"%c\",\"next\":\"%c\",\"trial\":%u,\"bootcount\":%u,"
                     "\"target\":\"%s\","
                     "\"state\":\"%s\",\"error\":%u,\"received\":%u,\"total\":%u,"
                     "\"backend_supported\":%s,\"backend_reason\":\"%s\","
                     "\"maintenance\":%u,\"rx\":%u,\"tx_queued\":%u,\"queue_rejected\":%u,"
                     "\"os_file\":\"d13x_os.itb\",\"candidate_capacity\":%u}",
                     OTA_PRODUCT, OTA_HARDWARE, OTA_VERSION, boot.slot, boot.next,
                     boot.trial ? 1u : 0u, (unsigned)boot.bootcount, snapshot.target,
                     meter_update_state_name(snapshot.state), (unsigned)snapshot.error,
                     (unsigned)snapshot.received, (unsigned)snapshot.total,
                     meter_aic_update_supported() ? "true" : "false",
                     meter_aic_update_reason(),
                     snapshot.maintenance, (unsigned)snapshot.rx,
                     (unsigned)snapshot.tx, (unsigned)snapshot.drops,
                     (unsigned)meter_aic_update_capacity());
    return n >= 0 && (size_t)n < capacity ? (size_t)n : 0;
}

static bool ota_can_reset(void *ctx)
{
    (void)ctx;
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    bool ok = ota_shared.state == METER_UPDATE_ACTIVATED;
    rt_mutex_release(&ota_lock);
    return ok;
}

static void ota_reset(void *ctx)
{
    (void)ctx;
    rt_hw_cpu_reset();
}

static void ota_cancel(void *ctx)
{
    (void)ctx;
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    ota_shared.cancel = true;
    rt_mutex_release(&ota_lock);
}

static void ota_session_maintenance(void *ctx, bool on)
{
    (void)ctx;
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    ota_shared.session_maintenance = on;
    rt_mutex_release(&ota_lock);
}

static rt_err_t ota_rx_indicate(rt_device_t dev, rt_size_t size)
{
    (void)dev;
    (void)size;
    rt_sem_release(&ota_rx_sem);
    return RT_EOK;
}

static void ota_rx_entry(void *arg)
{
    (void)arg;
    for (;;) {
        if (rt_sem_take(&ota_rx_sem, rt_tick_from_millisecond(100)) != RT_EOK) {
            rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
            bool stopping = ota_shared.stopping;
            rt_mutex_release(&ota_lock);
            if (stopping) {
                return;
            }
            continue;
        }
        for (;;) {
            struct rt_can_msg msg;
            rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
            bool stopping = ota_shared.stopping;
            rt_mutex_release(&ota_lock);
            if (stopping) {
                return;
            }
            msg.hdr = -1;
            if (rt_device_read(ota_can, 0, &msg, sizeof(msg)) != sizeof(msg)) {
                break;
            }
            if (msg.id == OTA_RX_ID && msg.ide == RT_CAN_STDID &&
                msg.rtr == RT_CAN_DTR && msg.len <= 8) {
                rt_mutex_take(&ota_isotp_lock, RT_WAITING_FOREVER);
                isotp_on_can_message(&ota_transport.phys_link, msg.data, msg.len);
                rt_mutex_release(&ota_isotp_lock);
                rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
                ota_shared.rx++;
                rt_mutex_release(&ota_lock);
            }
#if defined(AIC_LVGL_USE_CAN_CAPTURE) && AIC_LVGL_USE_CAN_CAPTURE
            /* This thread is the only can0 reader, so it also delivers the
             * host's screenshot trigger; the capture runs on the UI thread. */
            else if (msg.id == LV_AIC_CAN_CAPTURE_TRIGGER_ID && msg.ide == RT_CAN_STDID &&
                     msg.rtr == RT_CAN_DTR && msg.len >= 3 &&
                     !memcmp(msg.data, "CAP", 3)) {
                rt_kprintf("can capture trigger %s\n",
                           lv_aic_can_capture_request("can0") ? "queued" : "busy");
            }
#endif
        }
    }
}

static void ota_svc_entry(void *arg)
{
    (void)arg;
    for (;;) {
        rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
        bool stopping = ota_shared.stopping;
        /* Shell flag or a CAN programming session; the smoke endpoint has no product admission hook. */
        bool admitted = ota_shared.maintenance || ota_shared.session_maintenance;
        if (ota_shared.admitted && !admitted) {
            ota_shared.cancel = true;
        }
        ota_shared.admitted = admitted;
        rt_mutex_release(&ota_lock);
        if (stopping) {
            return;
        }
        /* Serialized with RX ingestion (single nesting direction:
         * service handlers may take ota_lock under this lock). */
        rt_mutex_take(&ota_isotp_lock, RT_WAITING_FOREVER);
        UDSServerPoll(&ota_uds.server);
        rt_mutex_release(&ota_isotp_lock);
        rt_thread_mdelay(5);
    }
}

static bool ota_ipc_ready;

/* IPC must exist before any shell/view/worker use: the endpoint may never
 * have started (taking an uninitialized static mutex faults). */
static bool ota_ipc_init(void)
{
    if (ota_ipc_ready) {
        return true;
    }
    if (rt_mutex_init(&ota_lock, "ota_lk", RT_IPC_FLAG_PRIO) != RT_EOK ||
        rt_mutex_init(&ota_isotp_lock, "ota_tp", RT_IPC_FLAG_PRIO) != RT_EOK ||
        rt_sem_init(&ota_rx_sem, "ota_rx", 0, RT_IPC_FLAG_FIFO) != RT_EOK ||
        rt_sem_init(&ota_result_sem, "ota_rs", 0, RT_IPC_FLAG_FIFO) != RT_EOK) {
        return false;
    }
    if (rt_mq_init(&ota_jobs, "ota_job", ota_job_pool, sizeof(meter_update_job_t),
                   sizeof(ota_job_pool), RT_IPC_FLAG_FIFO) != RT_EOK ||
        rt_mq_init(&ota_results, "ota_res", ota_result_pool, sizeof(ota_completion_t),
                   sizeof(ota_result_pool), RT_IPC_FLAG_FIFO) != RT_EOK) {
        rt_mutex_detach(&ota_lock);
        rt_mutex_detach(&ota_isotp_lock);
        rt_sem_detach(&ota_rx_sem);
        rt_sem_detach(&ota_result_sem);
        return false;
    }
    ota_ipc_ready = true;
    return true;
}

bool lv_aic_can_ota_start(void)
{
    meter_update_backend_t backend;
    meter_update_policy_t policy;
    if (!ota_ipc_init()) {
        return false;
    }
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    bool idle = !ota_shared.started && !ota_shared.threads;
    bool maintenance = ota_shared.maintenance;
    rt_mutex_release(&ota_lock);
    if (!idle) {
        /* Stop is terminal until reboot: static threads park, they are
         * never deleted or re-armed (flash safety over convenience). */
        return false;
    }
    memset(&ota_shared, 0, sizeof(ota_shared));
    /* Maintenance may be enabled before start; keep it across the reset. */
    ota_shared.maintenance = maintenance;
    backend = meter_aic_update_backend(&ota_backend_state);
    (void)meter_aic_update_prepare();
    policy.product = OTA_PRODUCT;
    policy.hardware = OTA_HARDWARE;
    policy.max_size = meter_aic_update_capacity();
    policy.timeout_ms = OTA_TIMEOUT_MS;
    if (!meter_update_init(&ota_service, &policy, &backend)) {
        return false;
    }
    {
        meter_uds_port_t port = {NULL, ota_submit, ota_result, ota_info,
                                 ota_can_reset, ota_reset, ota_cancel,
                                 ota_session_maintenance};
        if (UDSServerTpISOTpCInit(&ota_transport, OTA_RX_ID, OTA_TX_ID,
                                  UDS_TP_NOOP_ADDR) != UDS_OK ||
            !meter_uds_init(&ota_uds, &ota_transport.hdl, &port)) {
            return false;
        }
    }
    ota_can = rt_device_find("can0");
    if (ota_can == RT_NULL) {
        return false;
    }
    {
        /* NOTE: SET_BAUD takes the rate as a pointer-sized VALUE, not a
         * pointer (see drv_can.c); passing &baud silently mistunes. */
        rt_device_control(ota_can, RT_CAN_CMD_SET_BAUD, (void *)(uintptr_t)CAN500kBaud);
        rt_device_control(ota_can, RT_DEVICE_CTRL_SET_INT, RT_NULL);
    }
    /* CAN read/write paths require the INT_RX/INT_TX open flags. */
    if (rt_device_open(ota_can, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX |
                                    RT_DEVICE_FLAG_INT_TX) != RT_EOK) {
        return false;
    }
    rt_device_set_rx_indicate(ota_can, ota_rx_indicate);
    if (rt_thread_init(&ota_rx_thread, "ota_rx", ota_rx_entry, NULL,
                       ota_rx_stack, sizeof(ota_rx_stack), 20, 10) != RT_EOK ||
        rt_thread_init(&ota_svc_thread, "ota_svc", ota_svc_entry, NULL,
                       ota_svc_stack, sizeof(ota_svc_stack), 21, 10) != RT_EOK ||
        rt_thread_init(&ota_worker_thread, "ota_upd", ota_worker_entry, NULL,
                       ota_worker_stack, sizeof(ota_worker_stack), 25, 10) != RT_EOK) {
        rt_device_close(ota_can);
        return false;
    }
    rt_thread_startup(&ota_rx_thread);
    rt_thread_startup(&ota_svc_thread);
    rt_thread_startup(&ota_worker_thread);
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    ota_shared.started = true;
    ota_shared.threads = true;
    rt_mutex_release(&ota_lock);
    return true;
}

void lv_aic_can_ota_stop(void)
{
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    if (!ota_shared.started) {
        rt_mutex_release(&ota_lock);
        return;
    }
    ota_shared.stopping = true;
    ota_shared.admitted = false;
    rt_mutex_release(&ota_lock);
}

bool lv_aic_can_ota_started(void)
{
    return ota_shared.started;
}

bool lv_aic_can_ota_stopped(void)
{
    if (!ota_shared.started) {
        return true;
    }
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    bool value = ota_shared.stopped;
    rt_mutex_release(&ota_lock);
    return value;
}

void lv_aic_can_ota_view(meter_update_view_t *view)
{
    if (view == NULL) {
        return;
    }
    memset(view, 0, sizeof(*view));
    if (!ota_ipc_init()) {
        return;
    }
    rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
    view->visible = ota_shared.started;
    view->state = ota_shared.state;
    view->error = (uint32_t)ota_shared.error;
    view->received = ota_shared.received;
    view->total = ota_shared.total;
    memcpy(view->target_version, ota_shared.target, sizeof(view->target_version));
    memcpy(view->current_version, OTA_VERSION, sizeof(OTA_VERSION));
    rt_mutex_release(&ota_lock);
}

static int lv_aic_can_ota(int argc, char **argv)
{
    if (!ota_ipc_init()) {
        rt_kprintf("ota unavailable: IPC init failed\n");
        return -1;
    }
    if (argc >= 2 && !strcmp(argv[1], "status")) {
        struct rt_can_status st;
        meter_aic_boot_t boot = meter_aic_update_boot();
        memset(&st, 0, sizeof(st));
        rt_kprintf("ota started=%d stopped=%d state=%s error=%u rx=%u tx=%u drop=%u link=%d/%d\n",
                   lv_aic_can_ota_started(), lv_aic_can_ota_stopped(),
                   meter_update_state_name(ota_shared.state),
                   (unsigned)ota_shared.error, (unsigned)ota_shared.rx,
                   (unsigned)ota_shared.tx, (unsigned)ota_shared.drops,
                   (int)ota_transport.phys_link.send_status,
                   (int)ota_transport.phys_link.receive_status);
        rt_kprintf("boot slot=%c next=%c trial=%u bootcount=%u version=%s\n",
                   boot.slot, boot.next, boot.trial ? 1u : 0u,
                   (unsigned)boot.bootcount, OTA_VERSION);
        if (ota_can != RT_NULL &&
            rt_device_control(ota_can, RT_CAN_CMD_GET_STATUS, &st) == RT_EOK) {
            rt_kprintf("can snd=%u dsnd=%u errcode=%u ackerr=%u\n",
                       (unsigned)st.sndpkg, (unsigned)st.dropedsndpkg,
                       (unsigned)st.errcode, (unsigned)st.ackerrcnt);
        }
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "maintenance") &&
        (!strcmp(argv[2], "on") || !strcmp(argv[2], "off"))) {
        rt_mutex_take(&ota_lock, RT_WAITING_FOREVER);
        ota_shared.maintenance = !strcmp(argv[2], "on");
        rt_mutex_release(&ota_lock);
        rt_kprintf("ota maintenance %s; admission required for BEGIN\n", argv[2]);
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "info")) {
        /* Static: finsh thread stack (2 KB default) cannot hold the JSON
         * plus newlib printf frames; the shell is single-threaded. */
        static uint8_t text[896];
        size_t n = ota_info(NULL, text, sizeof(text));
        if (!n) {
            return -1;
        }
        for (size_t offset = 0; offset < n; offset += 64u) {
            size_t count = n - offset;
            if (count > 64u) {
                count = 64u;
            }
            rt_kprintf("%.*s", (int)count, (const char *)&text[offset]);
        }
        rt_kprintf("\n");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "start")) {
        if (lv_aic_can_ota_start()) {
            rt_kprintf("ota endpoint started\n");
        } else {
            rt_kprintf("ota start failed or already stopped; reboot to re-arm\n");
        }
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "confirm")) {
        /* Safe beside a running endpoint: in a trial boot the backend is
         * unsupported, so no worker session can be touching the ENV. */
        (void)ota_confirm();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "stop")) {
        lv_aic_can_ota_stop();
        rt_kprintf("ota stopping\n");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "ui") &&
        (!strcmp(argv[2], "show") || !strcmp(argv[2], "close"))) {
        lv_aic_can_ota_ui_request(!strcmp(argv[2], "show"));
        rt_kprintf("ota ui request queued\n");
        return 0;
    }
    rt_kprintf("lv_aic_can_ota start|stop|status|info|confirm|maintenance on/off|ui show/close\n");
    return -1;
}
MSH_CMD_EXPORT(lv_aic_can_ota, CAN-USB firmware update endpoint);
#else
bool lv_aic_can_ota_start(void)
{
    return false;
}
void lv_aic_can_ota_stop(void) {}
bool lv_aic_can_ota_started(void)
{
    return false;
}
bool lv_aic_can_ota_stopped(void)
{
    return true;
}
#endif
#endif
