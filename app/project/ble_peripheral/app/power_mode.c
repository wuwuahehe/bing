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

/* 充电优先：满电前 TEC 禁止；满电后 TEC 全开、停充 */
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
static u16 g_vbat_mv;             /* 滤波后的 VBAT(mV) */
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
#if !R2_PRODUCT_COOLING
    return 0;   /* 普通版无制冷功能 */
#else
    /* 客户需求：插电即允许制冷，与充电同时进行 */
    return (g_pwr_mode == PWR_CHARGING) || (g_pwr_mode == PWR_DIRECT);
#endif
}

bool power_mode_is_charging(void)
{
    return (g_pwr_mode == PWR_CHARGING);
}

u16 power_mode_get_vbat(void)
{
    return g_vbat_mv;
}

/* 1S 锂电电压->电量粗估，仅供调试参考 */
u8 power_mode_get_soc(void)
{
    static const u16 v[] = {4200, 4100, 4000, 3900, 3800, 3700, 3600, 3500, 3400, 3300, 3000};
    static const u8  s[] = {100,  90,   80,   70,   55,   40,   25,   15,   8,    3,    0};
    u8 i;

    if (g_vbat_mv >= v[0])  return 100;
    if (g_vbat_mv <= v[10]) return 0;
    for (i = 0; i < 10; i++) {
        if (g_vbat_mv <= v[i] && g_vbat_mv >= v[i + 1]) {
            u16 dv = v[i] - v[i + 1];
            u16 dn = v[i] - g_vbat_mv;
            return (u8)(s[i] - (u8)((u16)(s[i] - s[i + 1]) * dn / dv));
        }
    }
    return 0;
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
        g_vbat_mv = mv;
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
            /* 充满需连续确认，防无电池抖动 */
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
            g_pwr_mode  = (sta == CHARGE_STA_OFF_BUT_DC_IN) ? PWR_DIRECT : PWR_CHARGING;
        } else if (g_vbat_low_cnt >= VBAT_LOW_CONFIRM) {
            g_pwr_mode = PWR_OFF;
            bsp_tec_off();
            bsp_fan_set(0);
            printf("[PWR] VBAT low -> POWER OFF\n");
#if LP_PWROFF_EN
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
