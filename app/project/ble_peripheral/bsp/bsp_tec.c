#include "bsp_tec.h"
#include "driver_gpio.h"
#include "driver_saradc.h"

/* ==================== 硬件定义 ==================== */
#define TEC_PORT            GPIOB_REG
#define TEC_PIN             GPIO_PIN_4          // PB4 -> C_EN

#define ADC_CURRENT_PORT    GPIOA_REG
#define ADC_CURRENT_PIN     GPIO_PIN_7          // PA7 -> ADCI_M
#define ADC_CURRENT_CH      ADC_CHANNEL_PA7     // 根据实际芯片定义

#define R_SENSE             0.02f               // 采样电阻 0.02Ω
#define TEC_MAX_CURRENT_A   3.0f                // 最大允许电流 3A
#define ADC_REF_VOLTAGE     3.3f                // ADC 参考电压
#define ADC_FULL_SCALE      1023.0f             // 10 位 ADC 满量程

// 电流校准系数（需根据实际电路测量后填入）
// 如果 ADCI_M 输入到 ADC 的电压 = 电流 * R_SENSE * 系数，
// 则此处设为 1.0，否则按实际比例调整
#define CURRENT_CALIB_FACTOR 1.0f

/* ==================== 全局状态 ==================== */
static uint8_t tec_fault = 0;   // 过流故障标志

/* ==================== GPIO 初始化 ==================== */
static void bsp_tec_gpio_init(void)
{
    gpio_init_typedef gpio_init_struct;

    // 1. PB4 推挽输出，初始高电平（关闭制冷片）
    gpio_init_struct.gpio_pin  = TEC_PIN;
    gpio_init_struct.gpio_dir  = GPIO_DIR_OUTPUT;
    gpio_init_struct.gpio_fen  = GPIO_FEN_GPIO;
    gpio_init_struct.gpio_fdir = GPIO_FDIR_SELF;
    gpio_init_struct.gpio_mode = GPIO_MODE_DIGITAL;
    gpio_init_struct.gpio_drv  = GPIO_DRV_6MA;
    gpio_init_struct.gpio_pupd = GPIO_PUPD_NULL;    // 建议显式指定上下拉状态，这里设为无


    gpio_init(TEC_PORT, &gpio_init_struct);
    gpio_set_bits(TEC_PORT, TEC_PIN);   // 初始高电平，制冷片关闭

    // 2. PA7 模拟输入（ADC 电流检测）
    gpio_init_struct.gpio_pin  = ADC_CURRENT_PIN;
    gpio_init_struct.gpio_dir   = GPIO_DIR_INPUT;
    gpio_init_struct.gpio_fen  = GPIO_FEN_GPIO;
    gpio_init_struct.gpio_mode = GPIO_MODE_ANALOG;
    gpio_init_struct.gpio_fdir  = GPIO_FDIR_SELF;
    gpio_init_struct.gpio_pupd  = GPIO_PUPD_NULL;
    gpio_init_struct.gpio_drv   = GPIO_DRV_6MA;
    gpio_init(ADC_CURRENT_PORT, &gpio_init_struct);
}

/* ==================== ADC 初始化 ==================== */
static void adc_init(void)
{
    saradc_channel_init_typedef saradc_channel_init_struct;

    saradc_channel_init_struct.channel      = ADC_CURRENT_CH;
    saradc_channel_init_struct.pullup_en    = SARADC_PULLUP_DIS;
    saradc_channel_init_struct.setup_time   = SARADC_ST_8_CLK;   // 与 NTC 相同
    saradc_channel_init_struct.interrupt_en = DISABLE;

    saradc_channel_init(&saradc_channel_init_struct);
    saradc_kick_start(ADC_CURRENT_CH);

    while(!saradc_get_channel_flag(ADC_CURRENT_CH));

}

/* ==================== 电流读取 ==================== */
/**
 * @brief 读取当前制冷片电流
 * @return 电流值 (A)
 */
float bsp_tec_get_current(void)
{
    u16 adc_val;
    float voltage;

    // 启动一次 ADC 转换并等待完成
    saradc_kick_start(ADC_CURRENT_CH);
    // 等待转换完成（简单轮询，需根据实际 SDK 调整）
    while (!saradc_get_channel_flag(ADC_CURRENT_CH));
    adc_val = saradc_get_data(ADC_CURRENT_CH);

    // 转换为电压
    voltage = (float)adc_val * ADC_REF_VOLTAGE / ADC_FULL_SCALE;

    // 计算电流（需校准）
    float current = (voltage / R_SENSE) * CURRENT_CALIB_FACTOR;

    return current;
}

/* ==================== 控制接口 ==================== */
void bsp_tec_off(void)
{
    if (tec_fault) return;               // 故障时不允许开启
    gpio_reset_bits(TEC_PORT, TEC_PIN);  // PB4 = 0，PMOS 截止
}

void bsp_tec_on(void)
{
    gpio_set_bits(TEC_PORT, TEC_PIN);    // PB4 = 1，PMOS 导通
}

/* ==================== 安全监测 ==================== */
void bsp_tec_process(void)
{
    float current = bsp_tec_get_current();

    if (current > TEC_MAX_CURRENT_A) {
        tec_fault = 1;
        bsp_tec_off();   // 立即关闭
        // 可在此处添加错误处理，如 printf("TEC Overcurrent! Current=%.2fA\r\n", current);
    } else {
        tec_fault = 0;
    }
}

/* ==================== 初始化 ==================== */
void bsp_tec_init(void)
{
       bsp_tec_gpio_init();
    adc_init();
    tec_fault = 0;
    bsp_tec_off();   // 确保初始关闭
}
