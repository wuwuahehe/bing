#include "include.h"

#include "driver_charge.h"
#include "driver_gpio.h"

#include "bsp_tec.h"
#include "bsp_fan.h"
#include "bsp_saradc_vbat.h"

#include "power_mode.h"
#include "func_lowpwr.h"

#if BSP_CHARGE_EN

/* ==================== 可配参数（同步 PRD §6 / FR-PWR-06） ==================== */

#define PWR_PROC_PERIOD_MS      10      /* 调用周期，决定去抖计数换算 */
#define DC_DEBOUNCE_MS          200     /* 插拔电去抖（FR-MODE-02） */
#define DC_DEBOUNCE_CNT         (DC_DEBOUNCE_MS / PWR_PROC_PERIOD_MS)

#define VBAT_SAMPLE_MS          500     /* 电池采样周期 */
#define VBAT_FILTER_LEN         5       /* 滑动平均点数 */
#define VBAT_LOW_MV             3300    /* 低电阈值（FR-PWR-03） */
#define VBAT_LOW_CONFIRM        5       /* 连续 5 次（2.5s）才关机 */
#define VBAT_FULL_CONFIRM       10      /* 100ms 内连续判满，防单次抖动 */
#define DIRECT_MIN_DWELL_MS     2000    /* DIRECT 至少停留 2s 才允许回充 */

/* ==================== 内部状态 ==================== */

static pwr_mode_t g_pwr_mode = PWR_INIT;

static u8  g_dc_cnt;            /* 去抖计数 */
static u8  g_dc_online;         /* 去抖后的插电状态 */

static u32 g_vbat_tick;
static u16 g_vbat_buf[VBAT_FILTER_LEN];
static u8  g_vbat_idx;
static u8  g_vbat_filled;
static u8  g_vbat_low_cnt;
static u8  g_full_cnt;           /* 充满连续计数 */
static u32 g_mode_tick;          /* 进入当前状态的时刻 */

/* ==================== 工具函数 ==================== */

static u16 vbat_filter_push(u16 mv)
{
    u32 sum = 0;
    u8  i;

    g_vbat_buf[g_vbat_idx] = mv;
    g_vbat_idx++;
    if (g_vbat_idx >= VBAT_FILTER_LEN) {
        g_vbat_idx = 0;
        g_vbat_filled = 1;
    }

    if (g_vbat_filled) {
        for (i = 0; i < VBAT_FILTER_LEN; i++) {
            sum += g_vbat_buf[i];
        }
        return (u16)(sum / VBAT_FILTER_LEN);
    }

    /* 未填满前用已有样本平均 */
    sum = 0;
    for (i = 0; i < g_vbat_idx; i++) {
        sum += g_vbat_buf[i];
    }
    return g_vbat_idx ? (u16)(sum / g_vbat_idx) : mv;
}

/* 拔电瞬间必须立刻停执行器（FR-MODE-02：200ms 内） */
static void power_mode_enter_battery(void)
{
    bsp_tec_off();
    bsp_fan_set(0);
    g_vbat_low_cnt = 0;
    g_full_cnt    = 0;
    g_mode_tick   = tick_get();
    printf("[PWR] unplugged -> BATTERY, TEC/FAN off\n");
}

static void power_mode_enter_online(void)
{
    printf("[PWR] plugged -> %s\n",
           (charge_get_status() == CHARGE_STA_OFF_BUT_DC_IN) ? "DIRECT" : "CHARGING");
}

/* ==================== 对外接口 ==================== */

void power_mode_init(void)
{
    g_pwr_mode    = PWR_INIT;
    g_dc_cnt      = 0;
    g_dc_online   = 0;
    g_vbat_tick   = tick_get();
    g_vbat_idx    = 0;
    g_vbat_filled = 0;
    g_vbat_low_cnt = 0;
}

pwr_mode_t power_mode_get(void)
{
    return g_pwr_mode;
}

bool power_mode_allow_cool(void)
{
    /* 只有插电（充电中 / 直供）才允许 TEC 与风扇动作 */
    return (g_pwr_mode == PWR_CHARGING) || (g_pwr_mode == PWR_DIRECT);
}

bool power_mode_is_charging(void)
{
    return (g_pwr_mode == PWR_CHARGING);
}

bool power_mode_dc_online(void)
{
    return g_dc_online ? true : false;
}

void power_mode_process(void)
{
    u8  dc_raw;
    u8  sta;

    /* ---------- 1. 插电检测 + 去抖 ---------- */
    dc_raw = (charge_detect_dc() != CHARGE_DC_STATUS_OFF) ? 1 : 0;

    if (dc_raw != g_dc_online) {
        if (++g_dc_cnt >= DC_DEBOUNCE_CNT) {
            g_dc_cnt    = 0;
            g_dc_online = dc_raw;
            if (g_dc_online) {
                power_mode_enter_online();
            } else {
                power_mode_enter_battery();
            }
        }
    } else {
        g_dc_cnt = 0;
    }

    /* ---------- 2. 电池采样（500ms） ---------- */
    if (tick_check_expire(g_vbat_tick, VBAT_SAMPLE_MS)) {
        u16 mv;
        g_vbat_tick = tick_get();
        mv = vbat_filter_push(bsp_get_vbat_level(1));
        /* invalid sample guard: <1500mV means ADC not ready / fault */
        if (mv < 1500) {
            g_vbat_low_cnt = 0;
            return;
        }

        /*
         * 低电判定条件（比 PRD 更保守）：
         *   仅“未插电 + TEC 已停”时累计，避免 TEC 2A 拉低 VBAT 造成误关机。
         *   未插电时 TEC 本来就被禁止，因此不影响保护效果。
         */
        if (!g_dc_online && (mv < VBAT_LOW_MV)) {
            if (g_vbat_low_cnt < VBAT_LOW_CONFIRM) {
                g_vbat_low_cnt++;
            }
        } else {
            g_vbat_low_cnt = 0;
        }
    }

    /* ---------- 3. 状态迁移 ---------- */
    sta = charge_get_status();

    switch (g_pwr_mode) {
    case PWR_INIT:
        g_mode_tick = tick_get();
        g_full_cnt  = 0;
        if (g_dc_online) {
            g_pwr_mode = (sta == CHARGE_STA_OFF_BUT_DC_IN) ? PWR_DIRECT : PWR_CHARGING;
        } else {
            g_pwr_mode = PWR_BATTERY;
        }
        break;

    case PWR_CHARGING:
        if (!g_dc_online) {
            g_pwr_mode = PWR_BATTERY;
        } else if (sta == CHARGE_STA_OFF_BUT_DC_IN) {
            /* 满充判定需连续确认：无电池时 VBAT 会在 ms 级反复跳变 */
            if (g_full_cnt < VBAT_FULL_CONFIRM) {
                g_full_cnt++;
            }
            if (g_full_cnt >= VBAT_FULL_CONFIRM) {
                g_pwr_mode  = PWR_DIRECT;
                g_mode_tick = tick_get();
                printf("[PWR] charge full -> DIRECT\n");
            }
        } else {
            g_full_cnt = 0;
        }
        break;

    case PWR_DIRECT:
        if (!g_dc_online) {
            g_pwr_mode = PWR_BATTERY;
        } else if (sta >= CHARGE_STA_ON_TRICKLE) {
            /* 掉电重充：需在 DIRECT 停留足 2s，防无电池高频抖动 */
            if (tick_check_expire(g_mode_tick, DIRECT_MIN_DWELL_MS)) {
                g_pwr_mode  = PWR_CHARGING;
                g_mode_tick = tick_get();
                g_full_cnt  = 0;
                printf("[PWR] recharge -> CHARGING\n");
            }
        }
        break;

    case PWR_BATTERY:
        if (g_dc_online) {
            g_mode_tick = tick_get();
            g_full_cnt  = 0;
            g_pwr_mode = (sta == CHARGE_STA_OFF_BUT_DC_IN) ? PWR_DIRECT : PWR_CHARGING;
        } else if (g_vbat_low_cnt >= VBAT_LOW_CONFIRM) {
            g_pwr_mode = PWR_OFF;
            bsp_tec_off();
            bsp_fan_set(0);
            printf("[PWR] VBAT low -> POWER OFF\n");
#if LP_PWROFF_EN
            /* FR-PWR-03: low battery shutdown, DC plug wakes it up */
            func_pwroff();
#endif
        }
        break;

    case PWR_OFF:
        if (g_dc_online) {
            g_pwr_mode  = PWR_CHARGING;
            g_mode_tick = tick_get();
            g_full_cnt  = 0;
            printf("[PWR] wakeup by DC -> CHARGING\n");
        }
        break;

    default:
        g_pwr_mode = PWR_INIT;
        break;
    }

    /* ---------- 4. 兜底：非插电时强制关执行器 ---------- */
    if (!power_mode_allow_cool()) {
        bsp_tec_off();
        bsp_fan_set(0);
    }
}

#endif /* BSP_CHARGE_EN */
