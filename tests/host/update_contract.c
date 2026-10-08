/* SPDX-License-Identifier: Apache-2.0 */
/* Host contract for the vendored update state machine and package guard.
 * Pure logic, no flash, no CAN: a fake backend records calls, the package
 * guard only sees hostile input here (valid-package paths need the CPIO
 * packer; board runs cover them). */
#include "update/meter_update.h"
#include "update/meter_package.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned backend_calls[6];

static meter_update_error_t fake_begin(void *ctx, const meter_update_manifest_t *m)
{
    (void)ctx;
    (void)m;
    backend_calls[0]++;
    return METER_UPDATE_OK;
}

static meter_update_error_t fake_write(void *ctx, const uint8_t *data, size_t n)
{
    (void)ctx;
    (void)data;
    (void)n;
    backend_calls[1]++;
    return METER_UPDATE_OK;
}

static meter_update_error_t fake_verify(void *ctx, const meter_update_manifest_t *m)
{
    (void)ctx;
    (void)m;
    backend_calls[2]++;
    return METER_UPDATE_OK;
}

static meter_update_error_t fake_activate(void *ctx, const meter_update_manifest_t *m)
{
    (void)ctx;
    (void)m;
    backend_calls[3]++;
    return METER_UPDATE_OK;
}

static void fake_abort(void *ctx)
{
    (void)ctx;
    backend_calls[4]++;
}

static void make_manifest(meter_update_manifest_t *m, const char *product)
{
    memset(m, 0, sizeof(*m));
    snprintf(m->product, sizeof(m->product), "%s", product);
    snprintf(m->hardware, sizeof(m->hardware), "d50t-2-lite");
    snprintf(m->version, sizeof(m->version), "1.0.0");
    m->size = 1024;
}

int main(void)
{
    meter_firmware_update_t u;
    meter_update_policy_t policy = {"lvgl-aic-smoke", "d50t-2-lite", 4096, 1000};
    meter_update_backend_t backend = {NULL, fake_begin, fake_write, fake_verify,
                                      fake_activate, fake_abort};
    meter_update_manifest_t m;
    uint8_t block[METER_UPDATE_BLOCK_SIZE];
    memset(block, 0xA5, sizeof(block));

    assert(!meter_update_init(NULL, &policy, &backend));
    assert(meter_update_init(&u, &policy, &backend));
    assert(u.state == METER_UPDATE_IDLE);

    make_manifest(&m, "wrong-product");
    assert(meter_update_begin(&u, &m, true, 0) == METER_UPDATE_COMPATIBILITY);
    make_manifest(&m, "lvgl-aic-smoke");
    assert(meter_update_begin(&u, &m, false, 0) == METER_UPDATE_DENIED);
    m.size = 0;
    assert(meter_update_begin(&u, &m, true, 0) == METER_UPDATE_LENGTH);
    m.size = 1024;
    assert(meter_update_begin(&u, &m, true, 100) == METER_UPDATE_OK);
    assert(u.state == METER_UPDATE_DOWNLOADING && u.generation == 1);
    assert(meter_update_begin(&u, &m, true, 100) == METER_UPDATE_STATE);

    assert(meter_update_write(&u, 9, 0, block, 512, 200) == METER_UPDATE_SESSION);
    assert(meter_update_write(&u, 1, 512, block, 512, 200) == METER_UPDATE_ORDER);
    assert(u.state == METER_UPDATE_FAILED);
    assert(meter_update_begin(&u, &m, true, 300) == METER_UPDATE_OK);
    assert(meter_update_write(&u, 2, 0, block, 512, 300) == METER_UPDATE_OK);
    assert(meter_update_write(&u, 2, 512, block, 512, 300) == METER_UPDATE_OK);
    assert(u.state == METER_UPDATE_TRANSFERRED);
    assert(meter_update_verify(&u, 2, 400) == METER_UPDATE_OK);
    assert(u.state == METER_UPDATE_CANDIDATE);
    assert(meter_update_activate(&u, 2, false) == METER_UPDATE_NVM);
    assert(u.state == METER_UPDATE_WAIT_DURABLE);
    assert(meter_update_activate(&u, 2, true) == METER_UPDATE_OK);
    assert(u.state == METER_UPDATE_ACTIVATED);
    assert(meter_update_abort(&u, 2) == METER_UPDATE_STATE);

    assert(meter_update_init(&u, &policy, &backend));
    assert(meter_update_begin(&u, &m, true, 500) == METER_UPDATE_OK);
    assert(meter_update_abort(&u, 1) == METER_UPDATE_OK);
    assert(u.state == METER_UPDATE_ABORTED);

    assert(meter_update_begin(&u, &m, true, 600) == METER_UPDATE_OK);
    meter_update_tick(&u, 600 + 999);
    assert(u.state == METER_UPDATE_DOWNLOADING);
    meter_update_tick(&u, 600 + 1000);
    assert(u.state == METER_UPDATE_FAILED && u.error == METER_UPDATE_TIMEOUT);

    assert(!strcmp(meter_update_state_name(METER_UPDATE_IDLE), "IDLE"));

    {
        meter_package_guard_t guard;
        meter_package_policy_t ppolicy = {1024, 4096, "d13x_os.itb", "1.0.0"};
        uint8_t garbage[128];
        memset(garbage, 0xFF, sizeof(garbage));
        assert(meter_package_init(&guard, &ppolicy));
        assert(meter_package_feed(&guard, garbage, sizeof(garbage)) != METER_PACKAGE_OK);
        assert(meter_package_finish(&guard) != METER_PACKAGE_OK);
        assert(meter_package_feed(NULL, garbage, sizeof(garbage)) == METER_PACKAGE_ARGUMENT);
    }

    puts("PASS update state machine and package guard negatives");
    return 0;
}
