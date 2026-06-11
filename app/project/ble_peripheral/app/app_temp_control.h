#ifndef __APP_TEMP_CONTROL_H__
#define __APP_TEMP_CONTROL_H__

#include "include.h"

/* ==================== 单位与范围 ==================== */
#define TEMP_SET_MIN            2       // 设置温度最小值(℃)
#define TEMP_SET_MAX            20      // 设置温度最大值(℃)
#define TEMP_SET_DEFAULT        8       // 默认设置温度(℃)
#define TEMP_HYSTERESIS         2       // 滞后温度(℃): set±2

/* ==================== 系统状态机 ==================== */
typedef enum {
    TEMP_STATE_IDLE = 0,        // 正常运行: 显示NTC温度, 恒温控制
    TEMP_STATE_SETTING,         // 温度设置: 显示设置温度, 闪烁
} temp_state_t;

/* ==================== 温度单位 ==================== */
typedef enum {
    TEMP_UNIT_C = 0,            // 摄氏度
    TEMP_UNIT_F,                // 华氏度
} temp_unit_t;

/* ==================== TEC控制状态 ==================== */
typedef enum {
    TEC_STOP = 0,               // 制冷片停止
    TEC_RUN,                    // 制冷片运行
} tec_state_t;

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化温度控制系统
 */
void app_temp_control_init(void);

/**
 * @brief 获取当前显示温度值(已根据单位换算, 四舍五入为整数℃或℉)
 */
int app_temp_control_get_display_temp(void);

/**
 * @brief 获取当前设置温度值(℃)
 */
int app_temp_control_get_set_temp(void);

/**
 * @brief 获取温度单位: 0=℃, 1=℉
 */
temp_unit_t app_temp_control_get_unit(void);

/**
 * @brief 获取当前系统状态
 */
temp_state_t app_temp_control_get_state(void);

/**
 * @brief 处理PP按键短按: 非设置模式→切换单位; 设置模式→增加温度
 */
void app_temp_control_on_pp_short(void);

/**
 * @brief 处理PP按键长按(3秒): 进入/退出设置模式
 */
void app_temp_control_on_pp_long(void);

/**
 * @brief 主处理函数(在func_process中每秒调用一次)
 *        处理: NTC采样→计算→显示更新→TEC滞后控制→风扇控制
 */
void app_temp_control_process(void);

/**
 * @brief 获取是否处于闪烁状态(设置模式下, 每500ms翻转)
 */
u8 app_temp_control_get_flash_state(void);

#endif // __APP_TEMP_CONTROL_H__