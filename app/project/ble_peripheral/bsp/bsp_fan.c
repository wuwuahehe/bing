#include "include.h"
#include "driver_gpio.h"
#include "bsp_fan.h"

/**
 * @brief 风扇初始化
 */
void bsp_fan_init(void)
{
    gpio_init_typedef gpio_init_structure;

    // 1. 配置引脚参数
    gpio_init_structure.gpio_pin  = FAN_PIN;
    gpio_init_structure.gpio_dir  = GPIO_DIR_OUTPUT;   // 设置为输出模式
    gpio_init_structure.gpio_fen  = GPIO_FEN_GPIO;     // 复用功能选择 GPIO
    gpio_init_structure.gpio_fdir = GPIO_FDIR_SELF;    // 方向由 GPIO 控制
    gpio_init_structure.gpio_mode = GPIO_MODE_DIGITAL; // 数字模式
    gpio_init_structure.gpio_drv  = GPIO_DRV_6MA;      // 驱动能力 (如果 MOS 栅极电容大，可考虑改为 GPIO_DRV_24MA)
    gpio_init_structure.gpio_pupd = GPIO_PUPD_NULL;    // 建议显式指定上下拉状态，这里设为无

    // 2. 执行初始化
    gpio_init(FAN_PORT, &gpio_init_structure);

    // 3. 默认关闭风扇 (假设低电平关闭)
    gpio_reset_bits(FAN_PORT, FAN_PIN);
}

/**
 * @brief 风扇状态控制
 * @param enable: true 开启, false 关闭
 */
void bsp_fan_set(bool enable)
{
    if (enable) {
        // 开启风扇 -> 输出高电平
        gpio_set_bits(FAN_PORT, FAN_PIN);
    } else {
        // 关闭风扇 -> 输出低电平
        gpio_reset_bits(FAN_PORT, FAN_PIN);
    }
}