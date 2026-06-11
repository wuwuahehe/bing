#ifndef __BSP_TEC_H__
#define __BSP_TEC_H__

#include "include.h"

void bsp_tec_init(void);
void bsp_tec_on(void);
void bsp_tec_off(void);
float bsp_tec_get_current(void);       // 返回电流(A)
void bsp_tec_process(void);            // 放在主循环或定时器中定期调用

#endif