#ifndef METER_UPDATE_VIEW_H
#define METER_UPDATE_VIEW_H
#include <stdbool.h>
#include <stdint.h>
#define METER_UPDATE_VERSION_SIZE 48u
/** @brief 升级阶段；接收完成不等于候选验证或激活成功。 */
typedef enum
{
    METER_UPDATE_IDLE,
    METER_UPDATE_DOWNLOADING,
    METER_UPDATE_TRANSFERRED,
    METER_UPDATE_VERIFYING,
    METER_UPDATE_CANDIDATE,
    METER_UPDATE_WAIT_DURABLE,
    METER_UPDATE_ACTIVATING,
    METER_UPDATE_ACTIVATED,
    METER_UPDATE_CONFIRMED,
    METER_UPDATE_ABORTED,
    METER_UPDATE_FAILED
} meter_update_state_t;
/** @brief App 复制的只读升级视图；不包含协议、存储句柄或操作回调。 */
typedef struct
{
    bool visible;
    meter_update_state_t state;
    uint32_t received, total, error;
    char current_version[METER_UPDATE_VERSION_SIZE], target_version[METER_UPDATE_VERSION_SIZE];
} meter_update_view_t;
#endif
