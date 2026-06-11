#include "app_temp_control.h"
#include "bsp_ntc.h"
#include "bsp_display188.h"
#include "bsp_tec.h"
#include "bsp_fan.h"

/* ==================== 内部状态 ==================== */
static temp_state_t g_state = TEMP_STATE_IDLE;     // 系统状态
static temp_unit_t  g_unit  = TEMP_UNIT_C;          // 温度单位: 默认℃
static int  g_set_temp_c = TEMP_SET_DEFAULT;        // 设置温度(℃)
static int  g_actual_temp_c = 25;                   // 实际NTC温度(℃), 无效时=25
static tec_state_t g_tec = TEC_STOP;                // TEC状态
static u8   g_flash_on = 0;                         // 闪烁标志: 1=显示, 0=熄灭

/* ==================== 风扇控制策略 ==================== */
/*
 * 风扇与TEC联动:
 *   TEC运行时 → 风扇必须开 (散热)
 *   TEC停止 → 风扇延迟60s关闭 (排余热, 简化版直接关)
 *   实际可根据需要延时关闭
 */

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
 */
int app_temp_control_get_display_temp(void)
{
    if (g_state == TEMP_STATE_SETTING) {
        return (g_unit == TEMP_UNIT_C) ? g_set_temp_c : celsius_to_fahrenheit(g_set_temp_c);
    } else {
        // 实际温度四舍五入取整
        int t = (g_actual_temp_c + 5) / 10;
        return (g_unit == TEMP_UNIT_C) ? t : celsius_to_fahrenheit(t);
    }
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

void app_temp_control_on_pp_short(void)
{
    if (g_state == TEMP_STATE_SETTING) {
        // 设置模式: 每短按1次增加1°C, 2→20→2循环
        g_set_temp_c++;
        if (g_set_temp_c > TEMP_SET_MAX) {
            g_set_temp_c = TEMP_SET_MIN;
        }
        printf("[APP] Set temp=%d℃\r\n", g_set_temp_c);
    } else {
        // 正常运行模式: 切换单位
        g_unit = (g_unit == TEMP_UNIT_C) ? TEMP_UNIT_F : TEMP_UNIT_C;
        printf("[APP] Unit switch to %s\r\n", (g_unit == TEMP_UNIT_C) ? "C" : "F");
    }
}

void app_temp_control_on_pp_long(void)
{
    if (g_state == TEMP_STATE_IDLE) {
        // 长按进入设置模式
        g_state = TEMP_STATE_SETTING;
        g_flash_on = 1;
        printf("[APP] Enter setting mode, set=%d℃\r\n", g_set_temp_c);
    } else {
        // 长按退出设置模式
        g_state = TEMP_STATE_IDLE;
        g_flash_on = 1;
        printf("[APP] Exit setting mode, set=%d℃\r\n", g_set_temp_c);
        // 退出设置模式时刷新一次显示
    }
}

/* ==================== 显示更新 ==================== */

/**
 * @brief 更新数码管显示
 */
static void update_display(void)
{
    int disp_val;       // 显示值(单位已换算)
    int d1, d2, d3;     // 百位、十位、个位
    int dp1, dp2;       // 小数点指示灯: L1=℃, L2=℉

    disp_val = app_temp_control_get_display_temp();
    dp1 = (g_unit == TEMP_UNIT_C) ? 1 : 0;   // ℃亮
    dp2 = (g_unit == TEMP_UNIT_F) ? 1 : 0;   // ℉亮

    // 设置模式下闪烁控制
    if (g_state == TEMP_STATE_SETTING && g_flash_on == 0) {
        // 闪烁熄灭相: 显示全灭
        bsp_display188_set_digit(10, 10, 10, dp1, dp2);
        return;
    }

    // 显示数值
    if (disp_val > 99) {
        // 三位数 (如℉值超过100)
        d1 = disp_val / 100;
        d2 = (disp_val / 10) % 10;
        d3 = disp_val % 10;
    } else if (disp_val >= 10) {
        d1 = 10;                // 百位灭
        d2 = disp_val / 10;
        d3 = disp_val % 10;
    } else {
        d1 = 10;                // 百位灭
        d2 = 10;                // 十位灭
        d3 = disp_val;
    }

    bsp_display188_set_digit(d1, d2, d3, dp1, dp2);
}

/* ==================== TEC+风扇滞后控制 ==================== */

/**
 * @brief TEC滞后控制:
 *   NTC温度 > 设定温度 + 2℃ → TEC开启制冷
 *   NTC温度 < 设定温度 - 2℃ → TEC停止制冷
 *   风扇与TEC联动: TEC开则风扇开, TEC停则风扇停
 *
 *   set=15℃: TEC在NTC>17时开, 在NTC<13时停
 */
static void hysteresis_control(void)
{
    int ntc_raw = bsp_ntc_get_temperature();  // ℃×10
    int ntc_c;                                // 四舍五入取整℃

    if (ntc_raw == 0xFFFF || ntc_raw < -990 || ntc_raw > 990) {
        // NTC异常: 关闭TEC和风扇
        if (g_tec == TEC_RUN) {
            bsp_tec_off();
            bsp_fan_set(0);
            g_tec = TEC_STOP;
        }
        return;
    }

    ntc_c = (ntc_raw + 5) / 10;   // 四舍五入
    g_actual_temp_c = ntc_raw;     // 保留原始值供显示

    if (g_tec == TEC_RUN) {
        // TEC运行中: 检查是否需要停止 (低于设定-2℃)
        if (ntc_c <= (g_set_temp_c - TEMP_HYSTERESIS)) {
            // 已冷却到目标温度以下 → 停止TEC, 关闭风扇
            bsp_tec_off();
            bsp_fan_set(1);
            g_tec = TEC_STOP;
            printf("[TEC] OFF (ntc=%d <= set=%d-%d)\r\n", ntc_c, g_set_temp_c, TEMP_HYSTERESIS);
        }
    } else {
        // TEC停止中: 检查是否需要开启 (高于设定+2℃)
        if (ntc_c >= (g_set_temp_c + TEMP_HYSTERESIS)) {
            // 温度过高 → 开启TEC, 打开风扇
            bsp_tec_on();
            bsp_fan_set(1);
            g_tec = TEC_RUN;
            printf("[TEC] ON (ntc=%d >= set=%d+%d)\r\n", ntc_c, g_set_temp_c, TEMP_HYSTERESIS);
        }
    }
}

/* ==================== 初始化 ==================== */

void app_temp_control_init(void)
{
    g_state = TEMP_STATE_IDLE;
    g_unit  = TEMP_UNIT_C;
    g_set_temp_c = TEMP_SET_DEFAULT;
    g_actual_temp_c = 250;   // 默认25.0℃
    g_tec = TEC_STOP;
    g_flash_on = 1;

    // 确保TEC和风扇初始关闭(已由bsp_tec_init/bsp_fan_init处理)
    printf("[APP] Temp control init: set=%d c, unit=C\r\n", g_set_temp_c);
}

/* ==================== 主处理 ==================== */

void app_temp_control_process(void)
{
    // 1. NTC采样 (由bsp_ntc_process内部定时触发)
    //    已在 func_process 中调用 bsp_ntc_process()

    // 2. TEC+风扇滞后控制 (每秒执行一次)
    hysteresis_control();

    // 3. 设置模式下闪烁定时 (每500ms翻转, 由每秒2次process调用实现)
    //    实际闪烁在 func_process 中由500ms定时驱动
    //    这里通过 g_flash_on 翻转

    // 4. 刷新显示
    update_display();
}

/**
 * @brief 闪烁状态更新 (由外部每500ms调用一次)
 */
void app_temp_control_flash_toggle(void)
{
    if (g_state == TEMP_STATE_SETTING) {
        g_flash_on = !g_flash_on;
    } else {
        g_flash_on = 1;
    }
}