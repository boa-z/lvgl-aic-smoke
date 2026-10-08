#include "protocols/uds/meter_uds.h"
#include "util.h"
#include <string.h>
static UDSErr_t map_error(meter_update_error_t e)
{
    switch (e)
    {
    case METER_UPDATE_OK:
        return UDS_PositiveResponse;
    case METER_UPDATE_COMPATIBILITY:
    case METER_UPDATE_FORMAT:
    case METER_UPDATE_LENGTH:
        return UDS_NRC_RequestOutOfRange;
    case METER_UPDATE_DENIED:
    case METER_UPDATE_NVM:
    case METER_UPDATE_UNSUPPORTED:
        return UDS_NRC_ConditionsNotCorrect;
    case METER_UPDATE_ORDER:
    case METER_UPDATE_SESSION:
    case METER_UPDATE_STATE:
        return UDS_NRC_RequestSequenceError;
    default:
        return UDS_NRC_GeneralProgrammingFailure;
    }
}
static UDSErr_t dispatch(meter_uds_t *u, const meter_update_job_t *j)
{
    if (!u->pending)
    {
        if (!u->port.submit(u->port.context, j))
            return UDS_NRC_BusyRepeatRequest;
        u->pending = true;
        u->pending_kind = j->kind;
    }
    if (u->pending_kind != j->kind)
    {
        return UDS_NRC_RequestSequenceError;
    }
    meter_update_error_t e;
    if (!u->port.result(u->port.context, &e, &u->generation, &u->offset))
        return UDS_NRC_RequestCorrectlyReceived_ResponsePending;
    u->pending = false;
    return map_error(e);
}
static UDSErr_t event(UDSServer_t *srv, UDSEvent_t ev, void *arg)
{
    meter_uds_t *u = srv->fn_data;
    /* 活跃请求与 0x78 处理维持诊断会话，空闲时仍按 S3 超时。 */
    if (ev != UDS_EVT_SessionTimeout)
        srv->s3_session_timeout_timer = UDSMillis() + srv->s3_ms;
    meter_update_job_t j = {0};
    j.generation = u->generation;
    j.offset = u->offset;
    switch (ev)
    {
    case UDS_EVT_DiagSessCtrl:
    {
        UDSDiagSessCtrlArgs_t *a = arg;
        if (a->type != 1u && a->type != 2u)
            return UDS_NRC_SubFunctionNotSupported;
        if (a->type == 1u)
        {
            u->port.cancel(u->port.context);
            u->manifest_valid = false;
            u->pending = false;
            srv->xferIsActive = false;
        }
        return UDS_PositiveResponse;
    }
    case UDS_EVT_WriteDataByIdent:
    {
        UDSWDBIArgs_t *a = arg;
        if (srv->sessionType != 2u)
            return UDS_NRC_ConditionsNotCorrect;
        if (a->dataId != 0xf180u || a->len != METER_UDS_MANIFEST_SIZE || srv->xferIsActive || u->pending)
            return UDS_NRC_RequestOutOfRange;
        const uint8_t *p = a->data;
        meter_update_manifest_t m = {0};
        memcpy(m.product, p, 32);
        memcpy(m.hardware, p + 32, 32);
        memcpy(m.version, p + 64, 48);
        m.size = ((uint32_t)p[112] << 24) | ((uint32_t)p[113] << 16) | ((uint32_t)p[114] << 8) | p[115];
        memcpy(m.sha256, p + 116, 32);
        if (!memchr(m.product, 0, 32) || !memchr(m.hardware, 0, 32) || !memchr(m.version, 0, 48) || !m.size)
            return UDS_NRC_RequestOutOfRange;
        u->manifest = m;
        u->manifest_valid = true;
        return UDS_PositiveResponse;
    }
    case UDS_EVT_ReadDataByIdent:
    {
        UDSRDBIArgs_t *a = arg;
        if (a->dataId != 0xf180u)
            return UDS_NRC_RequestOutOfRange;
        size_t n = u->port.info(u->port.context, u->info, sizeof(u->info));
        if (n > sizeof(u->info))
            return UDS_NRC_ResponseTooLong;
        return a->copy(srv, u->info, (uint16_t)n);
    }
    case UDS_EVT_RequestDownload:
    {
        UDSRequestDownloadArgs_t *a = arg;
        if (srv->sessionType != 2u || !u->manifest_valid || a->addr || a->dataFormatIdentifier ||
            a->size != u->manifest.size)
            return UDS_NRC_RequestOutOfRange;
        a->maxNumberOfBlockLength = METER_UPDATE_BLOCK_SIZE + 2u;
        j.kind = METER_UPDATE_JOB_BEGIN;
        j.manifest = u->manifest;
        return dispatch(u, &j);
    }
    case UDS_EVT_TransferData:
    {
        UDSTransferDataArgs_t *a = arg;
        if (!a->len || a->len > sizeof(j.data))
            return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
        j.kind = METER_UPDATE_JOB_WRITE;
        j.size = a->len;
        memcpy(j.data, a->data, a->len);
        return dispatch(u, &j);
    }
    case UDS_EVT_RequestTransferExit:
    {
        UDSRequestTransferExitArgs_t *a = arg;
        if (a->len)
            return UDS_NRC_IncorrectMessageLengthOrInvalidFormat;
        j.kind = METER_UPDATE_JOB_VERIFY;
        return dispatch(u, &j);
    }
    case UDS_EVT_RoutineCtrl:
    {
        UDSRoutineCtrlArgs_t *a = arg;
        if (srv->sessionType != 2u || a->ctrlType != 1u || a->len)
            return UDS_NRC_RequestOutOfRange;
        if (a->id == 0xf001u)
            j.kind = METER_UPDATE_JOB_ACTIVATE;
        else if (a->id == 0xf002u)
            j.kind = METER_UPDATE_JOB_ABORT;
        else if (a->id == 0xf003u)
            j.kind = METER_UPDATE_JOB_CONFIRM;
        else
            return UDS_NRC_RequestOutOfRange;
        UDSErr_t e = dispatch(u, &j);
        if (e == UDS_PositiveResponse && j.kind == METER_UPDATE_JOB_ABORT)
        {
            srv->xferIsActive = false;
            u->manifest_valid = false;
        }
        return e;
    }
    case UDS_EVT_EcuReset:
    {
        UDSECUResetArgs_t *a = arg;
        if (a->type != 1u || !u->port.can_reset(u->port.context))
            return UDS_NRC_ConditionsNotCorrect;
        a->powerDownTimeMillis = 300;
        return UDS_PositiveResponse;
    }
    case UDS_EVT_DoScheduledReset:
        u->port.reset(u->port.context);
        return UDS_PositiveResponse;
    case UDS_EVT_SessionTimeout:
    {
        u->manifest_valid = false;
        u->pending = false;
        u->port.cancel(u->port.context);
        /* 使用上游初始化器一并清除旧的响应挂起、请求和传输上下文。 */
        UDSTp_t *transport = srv->tp;
        (void)UDSServerInit(srv);
        srv->tp = transport;
        srv->fn = event;
        srv->fn_data = u;
        return UDS_PositiveResponse;
    }
    default:
        return UDS_NRC_ServiceNotSupported;
    }
}
bool meter_uds_init(meter_uds_t *u, UDSTp_t *tp, const meter_uds_port_t *p)
{
    if (!u || !tp || !p || !p->submit || !p->result || !p->info || !p->can_reset || !p->reset || !p->cancel)
        return false;
    memset(u, 0, sizeof(*u));
    if (UDSServerInit(&u->server) != UDS_OK)
        return false;
    u->port = *p;
    u->server.fn = event;
    u->server.fn_data = u;
    u->server.tp = tp;
    return true;
}
