#include "update/meter_update.h"
#include <string.h>
/* 版本只允许稳定 ASCII 标识，避免诊断字符串注入或跨端编码歧义。 */
static bool valid_version(const char *version)
{
    for (size_t i = 0; version[i] != '\0'; ++i)
    {
        char c = version[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
              c == '_' || c == '+' || c == '-'))
            return false;
    }
    return version[0] != '\0';
}
static meter_update_error_t fail(meter_firmware_update_t *u, meter_update_error_t e)
{
    if (u->opened)
        u->backend.abort(u->backend.context);
    u->opened = false;
    u->error = e;
    u->state = METER_UPDATE_FAILED;
    return e;
}
bool meter_update_init(meter_firmware_update_t *u, const meter_update_policy_t *p,
                       const meter_update_backend_t *b)
{
    if (!u || !p || !b || !p->product || !p->hardware || !p->max_size || !p->timeout_ms ||
        p->timeout_ms >= UINT32_C(0x80000000) || strlen(p->product) >= METER_UPDATE_ID_SIZE ||
        strlen(p->hardware) >= METER_UPDATE_ID_SIZE || !b->begin || !b->write || !b->verify || !b->activate ||
        !b->abort)
        return false;
    memset(u, 0, sizeof(*u));
    u->policy = *p;
    u->backend = *b;
    return true;
}
meter_update_error_t meter_update_begin(meter_firmware_update_t *u, const meter_update_manifest_t *m,
                                        bool admitted, uint32_t now)
{
    if (!u || !m)
        return METER_UPDATE_FORMAT;
    if (u->state != METER_UPDATE_IDLE && u->state != METER_UPDATE_FAILED && u->state != METER_UPDATE_ABORTED)
        return METER_UPDATE_STATE;
    if (!admitted)
        return METER_UPDATE_DENIED;
    if (!memchr(m->product, 0, sizeof(m->product)) || !memchr(m->hardware, 0, sizeof(m->hardware)) ||
        !memchr(m->version, 0, sizeof(m->version)) || !valid_version(m->version))
        return METER_UPDATE_FORMAT;
    if (strcmp(m->product, u->policy.product) || strcmp(m->hardware, u->policy.hardware))
        return METER_UPDATE_COMPATIBILITY;
    if (!m->size || m->size > u->policy.max_size)
        return METER_UPDATE_LENGTH;
    if (u->generation == UINT32_MAX)
        return METER_UPDATE_SESSION;
    u->generation++;
    u->manifest = *m;
    u->received = 0;
    u->started_ms = now;
    u->last_activity = now;
    u->completed_ms = 0;
    u->error = METER_UPDATE_OK;
    /* begin 失败也可能留下资源，必须调用幂等 abort 清理。 */
    u->opened = true;
    meter_update_error_t e = u->backend.begin(u->backend.context, m);
    if (e != METER_UPDATE_OK)
        return fail(u, e);
    u->state = METER_UPDATE_DOWNLOADING;
    return METER_UPDATE_OK;
}
meter_update_error_t meter_update_write(meter_firmware_update_t *u, uint32_t gen, uint32_t offset,
                                        const uint8_t *data, size_t n, uint32_t now)
{
    if (!u || gen != u->generation)
        return METER_UPDATE_SESSION;
    if (u->state != METER_UPDATE_DOWNLOADING)
        return METER_UPDATE_STATE;
    if (offset != u->received)
        return fail(u, METER_UPDATE_ORDER);
    if (!data || !n || n > METER_UPDATE_BLOCK_SIZE || n > u->manifest.size - u->received)
        return fail(u, METER_UPDATE_LENGTH);
    meter_update_error_t e = u->backend.write(u->backend.context, data, n);
    if (e != METER_UPDATE_OK)
        return fail(u, e);
    u->received += (uint32_t)n;
    u->last_activity = now;
    if (u->received == u->manifest.size)
        u->state = METER_UPDATE_TRANSFERRED;
    return METER_UPDATE_OK;
}
meter_update_error_t meter_update_verify(meter_firmware_update_t *u, uint32_t gen, uint32_t now)
{
    if (!u || gen != u->generation)
        return METER_UPDATE_SESSION;
    if (u->state != METER_UPDATE_TRANSFERRED)
        return METER_UPDATE_STATE;
    u->state = METER_UPDATE_VERIFYING;
    meter_update_error_t e = u->backend.verify(u->backend.context, &u->manifest);
    if (e != METER_UPDATE_OK)
        return fail(u, e);
    u->state = METER_UPDATE_CANDIDATE;
    u->completed_ms = now;
    return METER_UPDATE_OK;
}
meter_update_error_t meter_update_activate(meter_firmware_update_t *u, uint32_t gen, bool durable)
{
    if (!u || gen != u->generation)
        return METER_UPDATE_SESSION;
    if (u->state != METER_UPDATE_CANDIDATE && u->state != METER_UPDATE_WAIT_DURABLE)
        return METER_UPDATE_STATE;
    if (!durable)
    {
        u->state = METER_UPDATE_WAIT_DURABLE;
        u->error = METER_UPDATE_NVM;
        return METER_UPDATE_NVM;
    }
    u->state = METER_UPDATE_ACTIVATING;
    meter_update_error_t e = u->backend.activate(u->backend.context, &u->manifest);
    if (e != METER_UPDATE_OK)
        return fail(u, e);
    u->backend.abort(u->backend.context);
    u->opened = false;
    u->state = METER_UPDATE_ACTIVATED;
    u->error = METER_UPDATE_OK;
    return METER_UPDATE_OK;
}
meter_update_error_t meter_update_abort(meter_firmware_update_t *u, uint32_t gen)
{
    if (!u || gen != u->generation)
        return METER_UPDATE_SESSION;
    if (u->state == METER_UPDATE_ACTIVATED || u->state == METER_UPDATE_CONFIRMED)
        return METER_UPDATE_STATE;
    if (u->opened)
        u->backend.abort(u->backend.context);
    u->opened = false;
    u->state = METER_UPDATE_ABORTED;
    u->error = METER_UPDATE_OK;
    return METER_UPDATE_OK;
}
void meter_update_tick(meter_firmware_update_t *u, uint32_t now)
{
    if (u && (u->state == METER_UPDATE_DOWNLOADING || u->state == METER_UPDATE_TRANSFERRED) &&
        (uint32_t)(now - u->last_activity) >= u->policy.timeout_ms)
        (void)fail(u, METER_UPDATE_TIMEOUT);
}
const char *meter_update_state_name(meter_update_state_t state)
{
    static const char *const names[] = {
        "IDLE",       "DOWNLOADING", "TRANSFERRED", "VERIFYING", "CANDIDATE_READY", "WAIT_DURABLE",
        "ACTIVATING", "ACTIVATED",   "CONFIRMED",   "ABORTED",   "FAILED"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "UNKNOWN";
}
