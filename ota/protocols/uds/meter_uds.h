#ifndef METER_UDS_H
#define METER_UDS_H
#include "server.h"
#include "update/meter_update.h"
#define METER_UDS_MANIFEST_SIZE 148u
/** @brief 语义任务由固定队列按值复制；不携带 UDS 接收缓存指针。 */
typedef enum
{
    METER_UPDATE_JOB_BEGIN,
    METER_UPDATE_JOB_WRITE,
    METER_UPDATE_JOB_VERIFY,
    METER_UPDATE_JOB_ACTIVATE,
    METER_UPDATE_JOB_ABORT,
    METER_UPDATE_JOB_CONFIRM
} meter_update_job_kind_t;
typedef struct
{
    meter_update_job_kind_t kind;
    uint32_t generation, offset;
    size_t size;
    meter_update_manifest_t manifest;
    uint8_t data[METER_UPDATE_BLOCK_SIZE];
} meter_update_job_t;
/** @brief Protocol owner 的异步端口；submit 只排队，完成结果保留到领取。 */
typedef struct
{
    void *context;
    bool (*submit)(void *, const meter_update_job_t *);
    bool (*result)(void *, meter_update_error_t *, uint32_t *, uint32_t *);
    size_t (*info)(void *, uint8_t *, size_t);
    bool (*can_reset)(void *);
    void (*reset)(void *);
    void (*cancel)(void *);
    /** @brief 可选：编程会话（0x10 02）请求维护模式，离开会话、S3 超时时撤销；Product 的准入钩子仍有最终决定权。 */
    void (*maintenance)(void *, bool);
} meter_uds_port_t;
/** @brief UDS 协议实例，不拥有 Flash 或 Domain。 */
typedef struct
{
    UDSServer_t server;
    meter_uds_port_t port;
    meter_update_manifest_t manifest;
    uint32_t generation, offset;
    bool manifest_valid, pending;
    meter_update_job_kind_t pending_kind;
    uint8_t info[896];
} meter_uds_t;
/** @brief 初始化固定版本 iso14229 server，由 Protocol thread 周期调用 Poll。 */
bool meter_uds_init(meter_uds_t *, UDSTp_t *, const meter_uds_port_t *);
#endif
