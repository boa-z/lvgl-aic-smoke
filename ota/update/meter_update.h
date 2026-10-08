#ifndef METER_UPDATE_H
#define METER_UPDATE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define METER_UPDATE_BLOCK_SIZE 512u
#define METER_UPDATE_ID_SIZE 32u
#include "contracts/meter_update_view.h"
/** @brief 可诊断错误；激活写入错误不能解释为已回滚。 */
typedef enum
{
    METER_UPDATE_OK,
    METER_UPDATE_STATE,
    METER_UPDATE_COMPATIBILITY,
    METER_UPDATE_DENIED,
    METER_UPDATE_LENGTH,
    METER_UPDATE_ORDER,
    METER_UPDATE_BACKEND,
    METER_UPDATE_HASH,
    METER_UPDATE_TIMEOUT,
    METER_UPDATE_NVM,
    METER_UPDATE_SESSION,
    METER_UPDATE_FORMAT,
    METER_UPDATE_UNSUPPORTED
} meter_update_error_t;
/** @brief 小型包描述；完整 SHA256 仅用于完整性，不提供身份认证。 */
typedef struct
{
    char product[METER_UPDATE_ID_SIZE], hardware[METER_UPDATE_ID_SIZE];
    char version[METER_UPDATE_VERSION_SIZE];
    uint32_t size;
    uint8_t sha256[32];
} meter_update_manifest_t;
/** @brief Product 明确选择升级身份、容量、超时和准入结果。 */
typedef struct meter_update_policy
{
    const char *product, *hardware;
    uint32_t max_size, timeout_ms;
} meter_update_policy_t;
/** @brief 只有独立 Update worker 调用此同步后端，禁止在协议回调中调用。 */
typedef struct
{
    void *context;
    meter_update_error_t (*begin)(void *, const meter_update_manifest_t *);
    meter_update_error_t (*write)(void *, const uint8_t *, size_t);
    meter_update_error_t (*verify)(void *, const meter_update_manifest_t *);
    meter_update_error_t (*activate)(void *, const meter_update_manifest_t *);
    void (*abort)(void *);
} meter_update_backend_t;
/** @brief worker 单一所有者；跨线程只能复制状态，不共享可变实例。 */
typedef struct
{
    meter_update_policy_t policy;
    meter_update_backend_t backend;
    meter_update_manifest_t manifest;
    meter_update_state_t state;
    meter_update_error_t error;
    uint32_t generation, received, last_activity, started_ms, completed_ms;
    bool opened;
} meter_firmware_update_t;
/** @brief 初始化有界服务，不访问 Flash。 */
bool meter_update_init(meter_firmware_update_t *, const meter_update_policy_t *,
                       const meter_update_backend_t *);
/** @brief 开始新会话；准入必须来自 Product/App，不能信任上位机声明。 */
meter_update_error_t meter_update_begin(meter_firmware_update_t *, const meter_update_manifest_t *,
                                        bool admitted, uint32_t now);
/** @brief 写入唯一下一块；generation/offset 隔离旧会话和乱序。 */
meter_update_error_t meter_update_write(meter_firmware_update_t *, uint32_t generation, uint32_t offset,
                                        const uint8_t *, size_t, uint32_t now);
/** @brief 完整接收后校验包与候选；未验证不得激活。 */
meter_update_error_t meter_update_verify(meter_firmware_update_t *, uint32_t generation, uint32_t now);
/** @brief 激活要求本地 NVM barrier 已满足；失败保留明确错误。 */
meter_update_error_t meter_update_activate(meter_firmware_update_t *, uint32_t generation, bool durable);
/** @brief 终止下载/候选；激活成功后不能用 Abort 伪装取消。 */
meter_update_error_t meter_update_abort(meter_firmware_update_t *, uint32_t generation);
/** @brief 检测下载超时并释放后端；无隐式重启。 */
void meter_update_tick(meter_firmware_update_t *, uint32_t now);
/** @brief 查询静态状态名称。 */
const char *meter_update_state_name(meter_update_state_t);
#endif
