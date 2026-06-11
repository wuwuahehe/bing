#ifndef _BSP_SARADC_NTC_H
#define _BSP_SARADC_NTC_H
#define BSP_NTC_EN                      1  
#include "driver_saradc.h"

/**
 * @brief NTC 参考电压通道配置
 *        使用 VRTC（内部RTC稳压输出~2.4V）作为ADC参考电压测量通道，
 *        用于校正ADC测量值，得到实际电压值（mV）
 */
#define NTC_REF_VOLTAGE_CHANNEL         ADC_CHANNEL_VRTC
#define NTC_REF_VOLTAGE_VALUE           pmu_get_vrtc24_mv()

/**
 * @brief NTC 分压电路参数
 *        R12 = 51KΩ（上拉电阻）
 *        VDDIO 为系统I/O供电电压，通过 pmu_get_vddio() 获取等级后计算
 *        VDDIO(mV) = 2400 + 100 * level
 */
#define NTC_PULLUP_RESISTOR             51000   // R12 = 51KΩ
#define NTC_VDDIO_MV_DEFAULT            3300    // 默认VDDIO=3.3V（若实际不同需修改）

/**
 * @brief NTC 热敏电阻参数（默认值，可修改）
 *        R0   = 10KΩ  (25°C时的标称阻值)
 *        B    = 3950  (B常数，常见NTC3950)
 *        T0   = 298.15K (25°C)
 */
#define NTC_R0                          10000   // 25°C时NTC阻值(Ω)
#define NTC_B_VALUE                     3950    // NTC B常数
#define NTC_T0_KELVIN                   29815   // 25°C * 100（定点数，避免浮点运算）
#define NTC_ABS_ZERO                    27315   // 绝对零度 * 100

/**
 * @brief ADC 采样参数
 */
#define NTC_SAMPLE_LOOP                 5       // 采样平均次数
#define NTC_SAMPLE_INTERVAL_MS          1000    // 采样间隔(ms)

/**
 * @brief NTC ADC通道 = PA6 = Channel 4
 */
#define NTC_ADC_CHANNEL                 ADC_CHANNEL_PA6

/**
 * @brief 初始化NTC温度检测
 *        配置PA6为模拟输入，初始化SARADC通道
 */
void bsp_ntc_init(void);

/**
 * @brief 获取NTC当前温度值
 * @return 温度值，单位：°C（返回实际温度×10，例：25.3°C返回253）
 *         若返回 0xFFFF 表示采样失败
 */
int bsp_ntc_get_temperature(void);

/**
 * @brief 获取NTC+引脚电压（mV）
 * @return 电压值，单位：mV
 */
uint16_t bsp_ntc_get_voltage(void);

/**
 * @brief NTC定时处理（需在主循环或定时器中周期性调用）
 */
void bsp_ntc_process(void);

#endif // _BSP_SARADC_NTC_H
