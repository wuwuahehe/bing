#include "app_temp_control.h"
#include "power_mode.h"
#include "bsp_ntc.h"
#include "bsp_display188.h"
#include "bsp_tec.h"
#include "bsp_fan.h"
#include "bsp_charge.h"
#include "bsp_saradc_vbat.h"

/* ==================== 内部状态 ==================== */
static temp_state_t g_state = TEMP_STATE_IDLE;     // 系统状态
static temp_unit_t  g_unit  = TEMP_UNIT_C;          // 温度单位: 默认℃
static int  g_set_temp_c = TEMP_SET_DEFAULT;        // 设置温度(℃)
static int  g_actual_temp_c = 25;                   // 实际NTC温度(℃), 无效时=25
static tec_state_t g_tec = TEC_STOP;                // TEC状态
static u8   g_flash_on = 0;                         // 闪烁标志: 1=显示, 0=熄灭

/* ==================== 设置超时 ==================== */
static u8   g_setting_idle_cnt = 0;                 // 设置模式无操作计数器(每100ms+1)

/* ==================== 迟滞消抖 ==================== */
static u8   g_tec_debounce_cnt = 0;                 // TEC切换消抖计数器

/* ==================== 显示锁定标志 ==================== */
static u8   g_display_locked = 0;                   // 0=初始终端显示NTC, 1=锁定显示设定温度
/* ---- 灭屏待机：仅电池模式有效（客户 P1/P2） ---- */
#define SCREEN_OFF_TIMEOUT_MS   5000

static int  g_disp_hold_c = -1;            /* 当前上屏的温度（℃） */
#define DISP_DELAY_MS           2000        /* 显示延时 2 秒 */
#define DISP_HIST_N             8
static int  g_hist_val[DISP_HIST_N];        /* 延时队列：温度值 */
static u32  g_hist_tick[DISP_HIST_N];       /* 延时队列：入队时刻 */
static u8   g_hist_wr, g_hist_rd, g_hist_cnt;
static u8   g_screen_off   = 0;   /* 1 = 灭屏待机 */
static u32  g_activity_tick = 0;  /* 最后一次操作时刻 */
                                                    // 用户长按进设置后,退出即锁定恒温显示

static u8   g_ntc_ready = 0;   // 【新增】：NTC真实数据就绪标志 (0=未就绪, 1=已拿到真实数据)
/* ==================== 温度换算 ==================== */

/**
 * @brief 摄氏度 → 华氏度 (℉ = ℃ × 9/5 + 32)
 */
static int celsius_to_fahrenheit(int c)
{
    return c * 9 / 5 + 32;
}

/**
 * @brief 获取显示用的整数值(已根据单位换算)
 *
 * 显示规则:
 *   - 初始终端状态(未锁定, TEMP_STATE_IDLE):   显示NTC实际温度
 *   - 设置模式(TEMP_STATE_SETTING):             显示设置温度(闪烁)
 *   - 恒温锁定(已锁定, TEMP_STATE_IDLE):        显示设定温度(恒温不变)
 *
 * 行为说明:
 *   上电后显示屏实时显示NTC采样温度(如24℃),
 *   用户长按进入设置模式后显示设定温度(闪烁可调),
 *   退出设置 → 显示锁定, 恒温显示设定温度(如15℃)不再变化.
 */
/* 新显示值入队（与上一个相同则不重复入队） */
static void display_delay_push(int c)
{
    u8 last;

    if (g_hist_cnt > 0) {
        last = (u8)((g_hist_wr + DISP_HIST_N - 1) % DISP_HIST_N);
        if (g_hist_val[last] == c) {
            return;
        }
    }

    if (g_hist_cnt >= DISP_HIST_N) {          /* 队列满：丢弃最旧一条 */
        g_hist_rd = (u8)((g_hist_rd + 1) % DISP_HIST_N);
        g_hist_cnt--;
    }

    g_hist_val[g_hist_wr]  = c;
    g_hist_tick[g_hist_wr] = tick_get();
    g_hist_wr = (u8)((g_hist_wr + 1) % DISP_HIST_N);
    if (g_hist_cnt < DISP_HIST_N) {
        g_hist_cnt++;
    }
}

/* 入队已满 2s 的值依次上屏 */
static void display_delay_update(void)
{
    while (g_hist_cnt > 0) {
        if (!tick_check_expire(g_hist_tick[g_hist_rd], DISP_DELAY_MS)) {
            break;
        }
        g_disp_hold_c = g_hist_val[g_hist_rd];
        g_hist_rd = (u8)((g_hist_rd + 1) % DISP_HIST_N);
        g_hist_cnt--;
    }
}

/* 模式切换时重置延时队列 */
static void display_delay_reset(void)
{
    g_disp_hold_c = -1;
    g_hist_wr  = 0;
    g_hist_rd  = 0;
    g_hist_cnt = 0;
}

/* BLE 与显示共用：返回摄氏整数的显示值（℃），钳位 2~50 */
int app_temp_control_get_display_temp_c(void)
{
    int disp;

    if (g_state == TEMP_STATE_SETTING) {
        disp = g_set_temp_c;          /* 设置模式：显示设定值 */
    } else if (g_display_locked) {
        disp = g_set_temp_c;          /* 恒温锁定：保持设定值 */
    } else {
        disp = (g_disp_hold_c >= 0) ? g_disp_hold_c : ((g_actual_temp_c + 5) / 10);   /* 稳定 2s 后的值 */
    }

    /* 客户需求 7：显示范围 2~50℃且钳位 */
    if (disp < 2)  disp = 2;
    if (disp > 50) disp = 50;

    /* 客户要求（插电）：显示温度不得低于设定温度
     *   即：插电时屏显 = max(实际显示值, 设定值)；拔电后显示实时温度。 */
#if R2_PRODUCT_COOLING
    if (power_mode_dc_online() && (disp < g_set_temp_c)) {
        disp = g_set_temp_c;
    }
#endif
    return disp;
}

int app_temp_control_get_display_temp(void)
{
    int c = app_temp_control_get_display_temp_c();

    /* 钳位在摄氏度侧完成，再转华氏度 */
    return (g_unit == TEMP_UNIT_C) ? c : celsius_to_fahrenheit(c);
}

/* BLE 协议用：实际温度（摄氏整数） */
/* BLE: NTC 是否已就绪（未就绪时上报编码 0） */
int app_temp_control_is_ntc_ready(void)
{
    return g_ntc_ready ? 1 : 0;
}

int app_temp_control_get_temp_c(void)
{
    int c = (g_actual_temp_c + 5) / 10;
    if (c < 2)  c = 2;
    if (c > 50) c = 50;
    return c;
}

/* BLE 下发设定温度：限制 2~50℃，并按界面刷新 */
void app_temp_control_set_temp_c(int c)
{
    if (c < TEMP_SET_MIN) c = TEMP_SET_MIN;
    if (c > 50)           c = 50;
    g_set_temp_c = c;
    g_display_locked = 0;
    g_tec_debounce_cnt = HYSTERESIS_DEBOUNCE_CNT;
}

int app_temp_control_get_set_temp(void)
{
    return g_set_temp_c;
}

temp_unit_t app_temp_control_get_unit(void)
{
    return g_unit;
}

temp_state_t app_temp_control_get_state(void)
{
    return g_state;
}

u8 app_temp_control_get_flash_state(void)
{
    return g_flash_on;
}

/* ==================== 按键处理 ==================== */

/**
 * @brief PP短按处理
 */
/* 记录一次用户操作，并唤醒灭屏 */
static void screen_activity_touch(void)
{
    g_activity_tick = tick_get();
    if (g_screen_off) {
        g_screen_off = 0;
        printf("[DISP] screen on\r\n");
    }
}

void app_temp_control_on_pp_short(void)
{
    if (g_state == TEMP_STATE_SETTING) {
        // ========== 设置模式: 增加温度 ==========
        if (g_unit == TEMP_UNIT_C) {
            g_set_temp_c++;
            if (g_set_temp_c > TEMP_SET_MAX) {
                g_set_temp_c = TEMP_SET_MIN;
            }
        } else {
            // ℉模式: 步进1℉, 范围36~68循环
            int f_now = celsius_to_fahrenheit(g_set_temp_c);
            f_now++;
            if (f_now > TEMP_SET_MAX_F) {
                f_now = TEMP_SET_MIN_F;
            }
            g_set_temp_c = (f_now - 32) * 5 / 9;
            int f_check = celsius_to_fahrenheit(g_set_temp_c);
            if (f_check < f_now && g_set_temp_c < TEMP_SET_MAX) {
                g_set_temp_c++;
            }
        }
        printf("[APP] Set temp=%d℃\r\n", g_set_temp_c);
        g_setting_idle_cnt = 0;

    } else {
        /* 客户 P2：待机（灭屏）单击点亮；亮屏单击仅刷新灭屏计时 */
        screen_activity_touch();
    }
}

/**
 * @brief PP长按处理
 */
/* 客户 P3：亮屏情况下双击切换 ℃/℉ */
void app_temp_control_on_pp_double(void)
{
    screen_activity_touch();
    if (g_state == TEMP_STATE_SETTING) {
        return;
    }
    g_unit = (g_unit == TEMP_UNIT_C) ? TEMP_UNIT_F : TEMP_UNIT_C;
    printf("[APP] Unit switch to %s\r\n", (g_unit == TEMP_UNIT_C) ? "C" : "F");
}

void app_temp_control_on_pp_long(void)
{
    /* 灭屏时长按只唤醒，不进设置（两个版本都适用） */
    if (g_screen_off) {
        screen_activity_touch();
        return;
    }
#if !R2_PRODUCT_COOLING
    /* 普通版无温控，点亮屏后长按不做任何事 */
    return;
#endif
    if (g_state == TEMP_STATE_IDLE) {
        // 长按进入设置模式
        g_state = TEMP_STATE_SETTING;
        g_flash_on = 1;
        g_setting_idle_cnt = 0;
        printf("[APP] Enter setting mode, set=%d℃\r\n", g_set_temp_c);

    } else if (g_state == TEMP_STATE_SETTING) {
        // 长按退出设置模式, 保存设置
        g_state = TEMP_STATE_IDLE;
    g_screen_off      = 0;
    g_activity_tick   = tick_get();
        g_flash_on = 1;
        g_setting_idle_cnt = 0;
        // 退出设置时解除显示锁定, 让屏幕跟随NTC温度变化
        // 直到NTC到达新设定的温度后, 才会重新锁定恒温显示
        g_display_locked = 0;
        printf("[APP] Exit setting mode, set=%d℃, display unlocked (wait NTC target)\r\n", g_set_temp_c);

        // 强制刷新TEC状态(应对死锁: 退出时NTC处于迟滞区间)
        g_tec_debounce_cnt = HYSTERESIS_DEBOUNCE_CNT;
    }
}

/* ==================== 显示更新 ==================== */

/**
 * @brief 更新数码管显示
 */
static void update_display(void)
{
    int disp_val;
    int d1, d2, d3;
    int dp1, dp2;

    /* 灭屏待机：数码管全灭 */
    if (g_screen_off) {
        bsp_display188_set_digit(10, 10, 10, 0, 0);
        return;
    }

    disp_val = app_temp_control_get_display_temp();
    dp1 = (g_unit == TEMP_UNIT_C) ? 1 : 0;
    dp2 = (g_unit == TEMP_UNIT_F) ? 1 : 0;

    /* ---- charging: unit segment blinks at 1Hz (FR-UI-05) ---- */
    if (power_mode_is_charging() && g_flash_on == 0) {
        dp1 = 0;
        dp2 = 0;
    }

    // 设置模式下闪烁
    if (g_state == TEMP_STATE_SETTING && g_flash_on == 0) {
        bsp_display188_set_digit(10, 10, 10, dp1, dp2);
        return;
    }
// 【新增拦截逻辑】：如果处于初始状态且真实的 NTC 数据还没算出来
    if (g_state == TEMP_STATE_IDLE && g_display_locked == 0 && g_ntc_ready == 0) {
        // 强制数码管全灭（假设 10 代表熄灭，这取决于您底层的驱动器实现）
        // 这通常只有开机后的头十几毫秒会进来，人眼看起来就像是屏幕还没亮起
        bsp_display188_set_digit(10, 10, 10, 0, 0); 
        return;
    }
    // 显示数值
    if (disp_val > 99) {
        d1 = disp_val / 100;
        d2 = (disp_val / 10) % 10;
        d3 = disp_val % 10;
    } else if (disp_val >= 10) {
        d1 = 10;
        d2 = disp_val / 10;
        d3 = disp_val % 10;
    } else {
        d1 = 10;
        d2 = 10;
        d3 = disp_val;
    }

    bsp_display188_set_digit(d1, d2, d3, dp1, dp2);
}

/* ==================== 风扇控制 ==================== */

/**
 * @brief 风扇控制策略
 *
 * 需求: 制冷片停止 → 风扇不停
 * 规则:
 *   - 系统上电后风扇持续运行(用于散热/通风)
 *   - TEC停止时不关闭风扇
 *   - 仅在NTC异常等安全场景下紧急关闭
 */
static void fan_control(void)
{
    static u8 fan_initialized = 0;
    /* not plugged: leave it to the process() fallback */
    if (!power_mode_allow_cool()) {
        return;
    }
    if (!fan_initialized) {
        fan_initialized = 1;
        printf("[FAN] ON\r\n");
    }
    bsp_fan_set(1);   /* re-assert: recover after re-plug */
}

/**
 * @brief 紧急关闭风扇(仅用于NTC故障安全场景)
 */
static void fan_emergency_off(void)
{
    bsp_fan_set(0);
    printf("[FAN] Emergency OFF\r\n");
}

/* ==================== TEC滞后控制 ==================== */

/**
 * @brief TEC滞后控制
 *
 * 逻辑:
 *   NTC ≥ (设定温度 + 2℃) → TEC开启制冷
 *   NTC ≤ (设定温度 - 2℃) → TEC停止制冷
 *
 * 示例(set=15℃):
 *   NTC=17℃ → TEC开
 *   NTC=13℃ → TEC停
 *   NTC=14~16℃迟滞区间 → TEC保持现有状态不变
 */
static void hysteresis_control(void)
{
    int ntc_raw = bsp_ntc_get_temperature();  // ℃×10
    int ntc_c;

    if (ntc_raw == 0xFFFF || ntc_raw < -990 || ntc_raw > 990) {
        // NTC异常: 仅关闭TEC, 不关闭风扇(需求:风扇不停)
        if (g_tec == TEC_RUN) {
            bsp_tec_off();
            g_tec = TEC_STOP;
            printf("[TEC] OFF (NTC fault)\r\n");
        }
        // 注意: 不调用 fan_emergency_off()
        // 风扇保持当前状态(持续运行)
        g_actual_temp_c = 250;
        g_tec_debounce_cnt = 0;
        return;
    }
    // 【新增这行】：走到这里说明 NTC 给出了合法的真实数据
    g_ntc_ready = 1;
    ntc_c = (ntc_raw + 5) / 10;
    g_actual_temp_c = ntc_raw;     // 保留用于显示NTC(初始终端状态)

    // 判断TEC期望状态
    tec_state_t target_state = g_tec;

    if (g_tec == TEC_RUN) {
        if (ntc_raw <= (g_set_temp_c * 10 - TEMP_HYSTERESIS_X10)) {
            target_state = TEC_STOP;
        }
    } else {
        if (power_mode_allow_cool() && ntc_raw >= (g_set_temp_c * 10 + TEMP_HYSTERESIS_X10)) {
            target_state = TEC_RUN;
        }
    }

    // 消抖处理
    if (target_state != g_tec) {
        g_tec_debounce_cnt++;
        if (g_tec_debounce_cnt >= HYSTERESIS_DEBOUNCE_CNT) {
            g_tec_debounce_cnt = 0;

            if (target_state == TEC_RUN) {
                bsp_tec_on();
                g_tec = TEC_RUN;
                printf("[TEC] ON (ntc10=%d >= set10=%d+%d)\r\n", ntc_raw, g_set_temp_c * 10, TEMP_HYSTERESIS_X10);
            } else {
                bsp_tec_off();
                g_tec = TEC_STOP;
                printf("[TEC] OFF (ntc10=%d <= set10=%d-%d)\r\n", ntc_raw, g_set_temp_c * 10, TEMP_HYSTERESIS_X10);
            }
            // 风扇由fan_control独立管理, TEC停止不影响风扇
        }
    } else {
        g_tec_debounce_cnt = 0;
    }
}

/* ==================== 设置超时自动退出 ==================== */

static void check_setting_timeout(void)
{
    if (g_state != TEMP_STATE_SETTING) {
        g_setting_idle_cnt = 0;
        return;
    }

    g_setting_idle_cnt++;
    if (g_setting_idle_cnt >= SETTING_TIMEOUT_STEPS) {
        g_state = TEMP_STATE_IDLE;
        g_flash_on = 1;
        g_setting_idle_cnt = 0;
        g_display_locked = 1;   // 超时退出也锁定恒温显示
        g_tec_debounce_cnt = HYSTERESIS_DEBOUNCE_CNT;
        printf("[APP] Setting timeout, auto exit, set=%d℃\r\n", g_set_temp_c);
    }
}

/* ==================== 初始化 ==================== */

void app_temp_control_init(void)
{
    int ntc_raw;
    int ntc_c;
    int i;

    g_state = TEMP_STATE_IDLE;
    g_unit  = TEMP_UNIT_C;
    g_set_temp_c = TEMP_SET_DEFAULT;
    g_actual_temp_c = 250;
    g_tec = TEC_STOP;
    g_flash_on = 1;
    g_setting_idle_cnt = 0;
    g_tec_debounce_cnt = 0;
    g_display_locked = 0;       // 初始未锁定, 显示NTC实际温度

    // 确保TEC初始关闭
    bsp_tec_off();

#if R2_PRODUCT_COOLING
    // 风扇: 系统上电即开启持续运行, 不再关闭
    bsp_fan_set(1);
    printf("[FAN] Always ON (system init)\r\n");
#else
    bsp_fan_set(0);   /* 普通版无风扇 */
#endif

    // 预充中值滤波缓冲区
    for (i = 0; i < NTC_MEDIAN_WINDOW; i++) {
        bsp_ntc_process();
    }

    // 死锁防护: 根据初始温度预判TEC
    ntc_raw = bsp_ntc_get_temperature();
    if (ntc_raw != 0xFFFF && ntc_raw >= -990 && ntc_raw <= 990) {
        ntc_c = (ntc_raw + 5) / 10;
        g_actual_temp_c = ntc_raw;

#if R2_PRODUCT_COOLING
        if (ntc_raw >= (g_set_temp_c * 10 + TEMP_HYSTERESIS_X10)) {
            bsp_tec_on();
            g_tec = TEC_RUN;
            printf("[TEC] Init ON (ntc10=%d >= set10=%d+%d)\r\n",
                   ntc_raw, g_set_temp_c * 10, TEMP_HYSTERESIS_X10);
        }
#endif
    }

    printf("[APP] Temp control init: set=%d℃, unit=C, ntc=%d, display_unlocked\r\n",
           g_set_temp_c, g_actual_temp_c);
}

/* ==================== 主处理 ==================== */

/**
 * @brief 检查NTC是否到达设定温度, 到达则锁定恒温显示
 *
 * 当用户退出设置模式后, g_display_locked=0,
 * 屏幕跟随NTC温度实时变化.
 * 一旦NTC到达或低于设定温度, 锁定显示为设定温度,
 * 此后屏幕不再变化(恒温显示).
 */
static void check_display_lock(void)
{
#if !R2_PRODUCT_COOLING
    return;   /* 普通版无设定温度，显示始终跟随实际温度 */
#endif
    if (g_state == TEMP_STATE_SETTING) {
        return;
    }

    if (g_display_locked) {
        return;
    }

    int ntc_c = (g_actual_temp_c + 5) / 10;
    
    // 【核心修复】：增加 g_tec == TEC_RUN 的判定
    // 只有在制冷片正在工作(降温中)，并且终于把温度压到了设定值及以下时，才锁定屏幕！
    // 如果开机室温就低于设定值，制冷片没启动(TEC_STOP)，则拒绝锁定，老老实实当温度计用！
    if (g_actual_temp_c <= (g_set_temp_c * 10) && g_tec == TEC_RUN) {
        g_display_locked = 1;
        printf("[DISP] Lock at %d℃ (ntc=%d℃)\r\n", g_set_temp_c, ntc_c);
    }
}

void app_temp_control_process(void)
{
    // 1. TEC滞后控制(基于NTC实际温度)
    hysteresis_control();

    // 2. 风扇控制(持续运行)
    fan_control();

    /* ---- non-plugged (battery / off): force actuators off, PRD FR-MODE-02 ---- */
    if (!power_mode_allow_cool()) {
        bsp_tec_off();
        bsp_fan_set(0);
        if (g_tec == TEC_RUN) {
            g_tec = TEC_STOP;
            g_tec_debounce_cnt = 0;
        }
        /* battery mode: show real temperature (client: touch -> real temp) */
        if (g_display_locked) {
            display_delay_reset();
        }
        g_display_locked = 0;
    }

    /* ---- 仅电池模式才灭屏；插电常亮（客户 P5：充电单位闪 / 充满常亮） ---- */
    if (power_mode_dc_online()) {
        g_screen_off = 0;
    } else if (!g_screen_off && g_state != TEMP_STATE_SETTING &&
               tick_check_expire(g_activity_tick, SCREEN_OFF_TIMEOUT_MS)) {
        g_screen_off = 1;
        printf("[DISP] screen off (battery, 5s)\r\n");
    }

    // 3. 设置模式超时检查
    check_setting_timeout();

    // 4. NTC到达设定温度检查(到达则锁定显示)
    check_display_lock();

    /* 每轮把当前温度候选值入队，2s 后才会上屏 */
    if (g_ntc_ready) {
        int cand = (g_actual_temp_c + 5) / 10;
        if (cand < 2)  cand = 2;
        if (cand > 50) cand = 50;
        display_delay_push(cand);
    }

    display_delay_update();

    // 5. 刷新显示
    update_display();

    /* ---- debug: 1 line per ~1s (process runs at 100ms) ---- */
    {
        static u8 dbg_cnt = 0;
        dbg_cnt++;
        if (dbg_cnt >= 10) {
            dbg_cnt = 0;
            printf("[T] ntc=%d vntc=%dmV vbat=%dmV vusb=%dmV soc=%d%% pwr=%d chg=%d disp=%d unit=%c set=%d tec=%d ready=%d\n",
                   bsp_ntc_get_temperature(), bsp_ntc_get_voltage(),
                   power_mode_get_vbat(), bsp_get_vusb_mv(), power_mode_get_soc(),
                   (int)power_mode_get(), (int)bsp_charge_get_status(),
                   app_temp_control_get_display_temp(),
                   (g_unit == TEMP_UNIT_C) ? 'C' : 'F',
                   g_set_temp_c, (int)g_tec, (int)g_ntc_ready);
        }
    }
}

/**
 * @brief 闪烁状态更新 (由外部每500ms调用一次)
 */
void app_temp_control_flash_toggle(void)
{
    /* charging or setting mode: 1Hz blink */
    if (g_state == TEMP_STATE_SETTING || power_mode_is_charging()) {
        g_flash_on = !g_flash_on;
    } else {
        g_flash_on = 1;
    }
}