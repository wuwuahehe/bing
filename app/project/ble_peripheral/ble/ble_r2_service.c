#include "include.h"

#include "ble_service.h"
#include "ble_r2_service.h"

#include "app_temp_control.h"

/* ==================== 参数 ==================== */

#define R2_REPORT_INTERVAL_MS   10000   /* 10s 定时上报（客户协议 4.1） */
#define R2_CHANGE_MIN_MS        200     /* 变化上报最小间隔，防 ADC 噪声刷屏 */
#define R2_CHANGE_STEP_CODE     5       /* 变化 >= 1.0℃（=5 个编码单位）才上报 */
#define R2_CODE_INVALID         0xFFFF

/* 连接参数：连上后 1.5s 发起，12ms -> 300ms（客户协议 4.4） */
#define R2_CONN_UPDATE_DELAY_MS 1500
#define R2_CONN_INTERVAL        240     /* 240 * 1.25ms = 300ms */
#define R2_CONN_LATENCY         0
#define R2_CONN_TIMEOUT         500     /* 500 * 10ms = 5s */

/* ---- 目标温度掉电保存：只在值真正变化时写，并去抖 2s ---- */
#define R2_NVM_SAVE_DEBOUNCE    2000    /* ms */
#define R2_NVM_MAGIC            0x52    /* 'R' */
#define R2_NVM_PAGE             BLE_CM_1_PAGE(0)   /* PAGE2，未被 SDK 占用 */

static u8  r2_nvm_dirty;
static u32 r2_nvm_tick;
static u8  r2_last_saved_temp = 0xFF;

static u8  r2_conn_pending;
static u32 r2_conn_tick;

#define R2_UNIT_C               0x01
#define R2_UNIT_F               0x02

/* ==================== 状态 ==================== */

static att_service_handler_t r2_service;
static uint16_t r2_notify_cccd;

static u32 r2_tick_10s;
static u32 r2_tick_chg;
static u16 r2_last_code = R2_CODE_INVALID;

/* ==================== 工具 ==================== */

/* CRC8: poly 0x07, init 0x00（与协议示例校验一致） */
static u8 r2_crc8(const u8 *data, u8 len)
{
    u8 crc = 0x00;
    u8 i;

    while (len--) {
        crc ^= *data++;
        for (i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? (u8)((crc << 1) ^ 0x07) : (u8)(crc << 1);
        }
    }
    return crc;
}

/* 摄氏整数 -> 编码（×5，0..250；超出按规格钳位） */
static u8 r2_code_from_c(int celsius)
{
    int code;

    if (celsius < 0) {
        celsius = 0;
    }
    if (celsius > 50) {
        celsius = 50;
    }
    code = celsius * 5;
    if (code > 250) {
        code = 250;
    }
    return (u8)code;
}

/* 编码 -> 摄氏整数（四舍五入） */
static int r2_c_from_code(u8 code)
{
    return (code + 2) / 5;
}

/* 组帧并发送 */
static void r2_send(u8 cmd, u8 code, u8 unit)
{
    u8 buf[4];

    if (!(r2_notify_cccd & 0x0001)) {
        return;                        /* 主机未订阅 */
    }
    if (!ble_cb.con_handle) {
        return;
    }

    buf[0] = cmd;
    buf[1] = code;
    buf[2] = unit;
    buf[3] = r2_crc8(&buf[1], 2);

    ble_notify_for_handle(ble_cb.con_handle,
                          ATT_CHARACTERISTIC_FFF2_01_VALUE_HANDLE,
                          buf, 4);
}

static u8 r2_unit_byte(void)
{
    return (app_temp_control_get_unit() == TEMP_UNIT_C) ? R2_UNIT_C : R2_UNIT_F;
}

static void r2_report_current(u8 cmd)
{
    int c;

    /* NTC not ready: send code 0, App discards this first frame */
    if (!app_temp_control_is_ntc_ready()) {
        r2_send(cmd, 0x00, r2_unit_byte());
        return;
    }
    c = app_temp_control_get_temp_c();
    r2_send(cmd, r2_code_from_c(c), r2_unit_byte());
}

static void r2_report_target(void)
{
    /* 0x83 frame: Byte2 is fixed 0x00 per protocol 7.2 (e.g. 83 28 00 06),
       NOT the unit byte */
    r2_send(R2_RPT_TARGET,
            r2_code_from_c(app_temp_control_get_set_temp()),
            0x00);
}

/* ==================== 掉电保存 ==================== */

static u8 r2_nvm_crc(u8 *d, u8 n)
{
    return r2_crc8(d, n);
}

static void r2_nvm_save(u8 set_temp)
{
    u8 buf[4];

    buf[0] = R2_NVM_MAGIC;
    buf[1] = set_temp;
    buf[2] = 0;
    buf[3] = r2_nvm_crc(buf, 3);

    cm_write(buf, R2_NVM_PAGE, sizeof(buf));
    cm_sync();
    r2_last_saved_temp = set_temp;
    printf("[BLE] nvm save set=%d\n", set_temp);
}

static void r2_nvm_load(void)
{
    u8 buf[4];

    cm_read(buf, R2_NVM_PAGE, sizeof(buf));
    if (buf[0] != R2_NVM_MAGIC || buf[3] != r2_nvm_crc(buf, 3)) {
        printf("[BLE] nvm empty/invalid\n");
        return;
    }
    if (buf[1] < 2 || buf[1] > 50) {
        printf("[BLE] nvm out of range %d\n", buf[1]);
        return;
    }
    r2_last_saved_temp = buf[1];
    app_temp_control_set_temp_c(buf[1]);
    printf("[BLE] nvm load set=%d\n", buf[1]);
}

/* ==================== 写回调 ==================== */

static int r2_write_callback(uint16_t con_handle, uint16_t attribute_handle,
                             uint16_t transaction_mode, uint16_t offset,
                             uint8_t *buffer, uint16_t buffer_size)
{
    u8 cmd;

    if (attribute_handle == ATT_CHARACTERISTIC_FFF2_01_CLIENT_CONFIGURATION_HANDLE) {
        r2_notify_cccd = GET_LE16(&buffer[0]);
        printf("[BLE] notify cccd=%04x\n", r2_notify_cccd);
        return ATT_ERR_NO_ERR;
    }

    if (attribute_handle != ATT_CHARACTERISTIC_FFF1_01_VALUE_HANDLE) {
        return ATT_ERR_NO_ERR;
    }
    if (buffer_size < 1) {
        return ATT_ERR_NO_ERR;
    }

    cmd = buffer[0];
    printf("[BLE] rx cmd=0x%02x len=%d\n", cmd, buffer_size);

    switch (cmd) {
    case R2_CMD_SET_TARGET:
        if (buffer_size >= 2) {
            int c = r2_c_from_code(buffer[1]);
            if (c != app_temp_control_get_set_temp()) {
                app_temp_control_set_temp_c(c);
                r2_nvm_dirty = 1;          /* 值真变化才写 */
                r2_nvm_tick  = tick_get(); /* 去抖：滑块流式下发只写最后一次 */
            }
            printf("[BLE] set target %d C (code %d)\n", c, buffer[1]);
        }
        /* 客户协议：0x01 无 ACK */
        break;

    case R2_CMD_QUERY_CUR:
        r2_report_current(R2_RPT_CURRENT);
        break;

    case R2_CMD_QUERY_TARGET:
        r2_report_target();
        break;

    default:
        printf("[BLE] unknown cmd 0x%02x\n", cmd);
        break;
    }

    lowpwr_sleep_delay_reset();
    lowpwr_pwroff_delay_reset();

    return ATT_ERR_NO_ERR;
}

static uint16_t r2_read_callback(uint16_t con_handle, uint16_t attribute_handle,
                                 uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    /* 本服务无只读特征，CCCD 由协议栈处理 */
    return 0;
}

static void r2_event_callback(uint8_t event_type, uint8_t *param, uint16_t size)
{
    if (event_type == BLE_EVT_CONNECT) {
        r2_conn_pending = 1;
        r2_conn_tick    = tick_get();
    } else if (event_type == BLE_EVT_DISCONNECT) {
        r2_notify_cccd  = 0;
        r2_conn_pending = 0;
    }
}

/* ==================== 对外接口 ==================== */

void ble_r2_service_init(void)
{
    printf("ble_r2_service_init\n");

    r2_service.start_handle   = ATT_SERVICE_FFF0_START_HANDLE;
    r2_service.end_handle     = ATT_SERVICE_FFF0_END_HANDLE;
    r2_service.read_callback  = &r2_read_callback;
    r2_service.write_callback = &r2_write_callback;
    r2_service.event_handler  = &r2_event_callback;
    att_server_register_service_handler(&r2_service);

    r2_notify_cccd = 0;
    r2_conn_pending = 0;
    r2_nvm_dirty   = 0;
    r2_last_code   = R2_CODE_INVALID;
    r2_tick_10s    = tick_get();
    r2_tick_chg    = tick_get();

    r2_nvm_load();   /* 上电恢复目标温度 */
}

void ble_r2_service_proc(void)
{
    u8 code;
    int diff;

    /* ---- connection parameter update: once, ~1.5s after connect ---- */
    if (r2_conn_pending && ble_cb.con_handle &&
        tick_check_expire(r2_conn_tick, R2_CONN_UPDATE_DELAY_MS)) {
        r2_conn_pending = 0;
        if (ble_update_conn_param(ble_cb.con_handle, R2_CONN_INTERVAL,
                                  R2_CONN_LATENCY, R2_CONN_TIMEOUT)) {
            printf("[BLE] conn param -> %dms\n", R2_CONN_INTERVAL * 125 / 100);
        } else {
            printf("[BLE] conn param update failed\n");
        }
    }

    /* ---- 掉电保存：变化后静止 2s 再写 ---- */
    if (r2_nvm_dirty && tick_check_expire(r2_nvm_tick, R2_NVM_SAVE_DEBOUNCE)) {
        u8 t = (u8)app_temp_control_get_set_temp();
        r2_nvm_dirty = 0;
        if (t != r2_last_saved_temp) {
            r2_nvm_save(t);
        }
    }

    if (!(r2_notify_cccd & 0x0001) || !ble_cb.con_handle) {
        return;
    }

    if (!app_temp_control_is_ntc_ready()) {
        code = 0x00;
    } else {
        code = r2_code_from_c(app_temp_control_get_temp_c());
    }

    /* 10s 定时上报 */
    if (tick_check_expire(r2_tick_10s, R2_REPORT_INTERVAL_MS)) {
        r2_tick_10s  = tick_get();
        r2_tick_chg  = r2_tick_10s;
        r2_last_code = code;
        r2_send(R2_RPT_REALTIME, code, r2_unit_byte());
        return;
    }

    /* 温度变化 >= 1.0℃ 立即上报（带最小间隔） */
    if (r2_last_code == R2_CODE_INVALID) {
        r2_last_code = code;
        return;
    }

    diff = (int)code - (int)r2_last_code;
    if (diff < 0) {
        diff = -diff;
    }
    if (diff >= R2_CHANGE_STEP_CODE && tick_check_expire(r2_tick_chg, R2_CHANGE_MIN_MS)) {
        r2_tick_chg  = tick_get();
        r2_last_code = code;
        r2_send(R2_RPT_REALTIME, code, r2_unit_byte());
    }
}
