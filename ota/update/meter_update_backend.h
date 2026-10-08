#ifndef METER_UPDATE_BACKEND_H
#define METER_UPDATE_BACKEND_H
#include "update/meter_update.h"
/** @brief SDK 后端组合句柄；原生全局状态只能由单一 Update worker 拥有。 */
typedef struct
{
    uint32_t rejected_sessions;
} meter_aic_update_t;
/** @brief 返回当前构建选定后端；默认拒绝写入，可选组合使用原生 OS-only 安装器。 */
meter_update_backend_t meter_aic_update_backend(meter_aic_update_t *);
/** @brief 查询当前 SDK 是否满足应用要求的完整升级契约。 */
bool meter_aic_update_supported(void);
/** @brief 返回稳定的能力阻塞原因，不将自动确认当作健康确认。 */
const char *meter_aic_update_reason(void);
/** @brief 试运行槽位（upgrade_available=1 且 osAB_now==osAB_next）通过健康门禁后显式确认；
 *         成功返回 0，非试运行或持久化校验失败返回负值。 */
int meter_aic_update_confirm(void);
#ifdef METER_AIC_OTA
/** @brief 启动审计时缓存的 A/B 引导状态；未读到 ENV 时 slot/next 为 '?'。 */
typedef struct
{
    char slot, next;
    bool trial;
    uint32_t bootcount;
} meter_aic_boot_t;
/** @brief 返回最近一次 prepare/confirm 读取的引导状态快照。 */
meter_aic_boot_t meter_aic_update_boot(void);
/** @brief 启动时只读审计本机 ENV 与候选 NAND 几何，不执行擦写。 */
bool meter_aic_update_prepare(void);
/** @brief 返回经本机审计的非活动 OS 分区容量。 */
uint32_t meter_aic_update_capacity(void);
#endif
#endif
