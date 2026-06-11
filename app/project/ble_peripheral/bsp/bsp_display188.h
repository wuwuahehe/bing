#ifndef __BSP_DISPLAY188_H__
#define __BSP_DISPLAY188_H__

#include "include.h"

#define BSP_DISPLAY188_EN   1   // 188数码管使能

#if BSP_DISPLAY188_EN

// GPIO引脚映射: PA0=Pin1, PA1=Pin2, PA2=Pin3, PA3=Pin4, PA4=Pin5, PA5=Pin6
#define DISP_PIN_ALL    (GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5)

void bsp_display188_init(void);
void bsp_display188_set_digit(int d1, int d2, int d3, int dp1, int dp2);
void bsp_display188_set_temp(int temp_x10);    // 设置温度(°C×10, 0xFFFF=异常)
void bsp_display188_process(void);             // 放在5ms定时器中扫描

#endif // BSP_DISPLAY188_EN

#endif // __BSP_DISPLAY188_H__