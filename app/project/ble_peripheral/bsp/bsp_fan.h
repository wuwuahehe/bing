#ifndef _BSP_FAN_H
#define _BSP_FAN_H
#define BSP_FAN_EN                      1  

#define FAN_PORT    GPIOB_REG      // 根据实际 PCB 走线调整
#define FAN_PIN     GPIO_PIN_0     // 假连接风扇控制 MOS 管
void bsp_fan_init(void);
void bsp_fan_set(bool enable);
#endif // _BSP_FAN_H
