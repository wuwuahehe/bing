#include "bsp_display188.h"
#include "driver_gpio.h"

/*
 * 188数码管 查理复用 (Charlieplexing) 驱动
 *
 * 引脚映射: PA0=Pin1, PA1=Pin2, PA2=Pin3, PA3=Pin4, PA4=Pin5, PA5=Pin6
 *
 * 扫描策略: 批量扫描 + 短延时保持
 *   一次 bsp_display188_process() 调用遍历全部23段.
 *   需要亮的段: light_led() + delay_us(800) 保持亮度
 *   不需要亮的段: 快速跳过 (仅几条指令)
 *   全部扫描完进入 Hi-Z
 *
 *   蓝牙空闲时: func_process ~1kHz → 每帧 ~12ms → 83Hz, 无闪烁
 *   蓝牙忙时:   func_process 频率降低, 但一次扫描只阻塞 ~800us×亮段数
 *              (通常亮8-10段 → 6-8ms), 远小于BLE连接间隔(30-50ms)
 *
 * 关键特性:
 *   - 不使用硬件定时器中断 (会与BLE射频中断冲突导致崩溃)
 *   - 不使用逐段扫描 (蓝牙忙时帧率太低导致闪烁)
 *   - 一次阻塞完成全帧, 只要总耗时 < 15ms 即不影响 BLE 连接
 *   - drv 寄存器每次 light_led 都恢复 (按键扫描会清除它)
 *   - dir 寄存器: 0=输出, 1=输入/Hi-Z
 */

/* ==================== 查理复用映射表 ==================== */
typedef struct {
    u16 anode;   // 输出高的引脚 (GPIO_PIN_x)
    u16 cathode; // 输出低的引脚 (GPIO_PIN_x)
} charlie_pair_t;

enum {
    LED_1A = 0, LED_1B, LED_1C, LED_1D, LED_1E, LED_1F, LED_1G, LED_L1,
    LED_2A, LED_2B, LED_2C, LED_2D, LED_2E, LED_2F, LED_2G, LED_L2,
    LED_3A, LED_3B, LED_3C, LED_3D, LED_3E, LED_3F, LED_3G,
    LED_TOTAL       // 23
};

/*
 * 查理复用引脚对表: {anode, cathode}
 *   1A: PA1=H,PA0=L  1B: PA2=H,PA0=L  1C: PA3=H,PA0=L
 *   1D: PA4=H,PA1=L  1E: PA3=H,PA1=L  1F: PA2=H,PA1=L
 *   1G: PA0=H,PA1=L  L1: PA4=H,PA0=L
 *   2A: PA0=H,PA2=L  2B: PA1=H,PA2=L  2C: PA3=H,PA2=L
 *   2D: PA4=H,PA3=L  2E: PA2=H,PA3=L  2F: PA1=H,PA3=L
 *   2G: PA0=H,PA3=L  L2: PA4=H,PA2=L
 *   3A: PA0=H,PA4=L  3B: PA1=H,PA4=L  3C: PA2=H,PA4=L
 *   3D: PA3=H,PA4=L  3E: PA5=H,PA4=L  3F: PA2=H,PA5=L
 *   3G: PA0=H,PA5=L
 */
static const charlie_pair_t led_table[LED_TOTAL] = {
    /* Digit 1 (百位) + L1 */
    {GPIO_PIN_1, GPIO_PIN_0},   // LED_1A
    {GPIO_PIN_2, GPIO_PIN_0},   // LED_1B
    {GPIO_PIN_3, GPIO_PIN_0},   // LED_1C
    {GPIO_PIN_4, GPIO_PIN_1},   // LED_1D
    {GPIO_PIN_3, GPIO_PIN_1},   // LED_1E
    {GPIO_PIN_2, GPIO_PIN_1},   // LED_1F
    {GPIO_PIN_0, GPIO_PIN_1},   // LED_1G
    {GPIO_PIN_4, GPIO_PIN_0},   // LED_L1 (℃)
    /* Digit 2 (十位) + L2 */
    {GPIO_PIN_0, GPIO_PIN_2},   // LED_2A
    {GPIO_PIN_1, GPIO_PIN_2},   // LED_2B
    {GPIO_PIN_3, GPIO_PIN_2},   // LED_2C
    {GPIO_PIN_4, GPIO_PIN_3},   // LED_2D
    {GPIO_PIN_2, GPIO_PIN_3},   // LED_2E
    {GPIO_PIN_1, GPIO_PIN_3},   // LED_2F
    {GPIO_PIN_0, GPIO_PIN_3},   // LED_2G
    {GPIO_PIN_4, GPIO_PIN_2},   // LED_L2 (℉)
    /* Digit 3 (个位) */
    {GPIO_PIN_0, GPIO_PIN_4},   // LED_3A
    {GPIO_PIN_1, GPIO_PIN_4},   // LED_3B
    {GPIO_PIN_2, GPIO_PIN_4},   // LED_3C
    {GPIO_PIN_3, GPIO_PIN_5},   // LED_3D  (修正阴极 PA5)
    {GPIO_PIN_2, GPIO_PIN_5},   // LED_3E  (修正阴极 PA5)
    {GPIO_PIN_1, GPIO_PIN_5},   // LED_3F  (修正阴极 PA5)
    {GPIO_PIN_0, GPIO_PIN_5},   // LED_3G
};

#define D1_BASE   0
#define D2_BASE   8
#define D3_BASE   16

/* 7段码 A=bit0..G=bit6 */
static const u8 seg7_table[10] = {
    0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
    0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111,
};

/* 显示缓冲区 */
static u8 disp_digit[3];
static u8 disp_dp1;
static u8 disp_dp2;

/* ==================== 核心操作 ==================== */

/* 点亮一个LED段: 阳极=H, 阴极=L, 其余Hi-Z */
static void light_led(u16 anode, u16 cathode)
{
    u16 mask = anode | cathode;

    // 1. 全部Hi-Z (dir=1=输入)
    GPIOA_REG->dir |= DISP_PIN_ALL;

    // 2. 写输出锁存值
    GPIOA_REG->io_clr = mask;
    GPIOA_REG->io_set = anode;

    // 3. 恢复 drv (按键扫描/其他模块可能清掉)
    GPIOA_REG->drv &= ~mask;

    // 4. 仅 anode+cathode 输出 (dir=0)
    GPIOA_REG->dir &= ~mask;
}

/* 全部Hi-Z */
static void all_hiz(void)
{
    GPIOA_REG->dir |= DISP_PIN_ALL;
}

/* 某段是否该亮 */
static u8 seg_on(u8 dig, u8 bit)
{
    u8 v = disp_digit[dig];
    if (v > 9) return 0;
    return (seg7_table[v] >> bit) & 1;
}

/* ==================== 批量扫描 ==================== */

/*
 * 一次扫描全部23段. 需要亮的段用 light_led + delay_us 保持,
 * 不需要的快速跳过.
 *
 * 总耗时 ≈ 亮段数 × SEG_HOLD_US (跳过段 < 1us)
 * 典型值: 8段亮 × 800us = 6.4ms, 23段全亮 = 18.4ms
 *
 * SEG_HOLD_US 增大则亮度+但阻塞时间+.
 * 800us 在蓝牙连接间隔(30ms+)内绰绰有余.
 */
#define SEG_HOLD_US  800

static void batch_scan(void)
{
    u8 idx;

    for (idx = 0; idx < LED_TOTAL; idx++) {
        u8 on;

        if (idx <= LED_L1) {
            on = (idx == LED_L1) ? disp_dp1 : seg_on(0, idx - D1_BASE);
        } else if (idx <= LED_L2) {
            on = (idx == LED_L2) ? disp_dp2 : seg_on(1, idx - D2_BASE);
        } else {
            on = seg_on(2, idx - D3_BASE);
        }

        if (on) {
            light_led(led_table[idx].anode, led_table[idx].cathode);
            delay_us(SEG_HOLD_US);
        }
    }

    all_hiz();
}

/* ==================== 公开接口 ==================== */

void bsp_display188_init(void)
{
    gpio_init_typedef gpio_init_structure;

    gpio_init_structure.gpio_pin  = DISP_PIN_ALL;
    gpio_init_structure.gpio_dir  = GPIO_DIR_OUTPUT;
    gpio_init_structure.gpio_fen  = GPIO_FEN_GPIO;
    gpio_init_structure.gpio_fdir = GPIO_FDIR_SELF;
    gpio_init_structure.gpio_mode = GPIO_MODE_DIGITAL;
    gpio_init_structure.gpio_drv  = GPIO_DRV_6MA;
    gpio_init(GPIOA_REG, &gpio_init_structure);

    GPIOA_REG->io_clr = DISP_PIN_ALL;
    GPIOA_REG->dir   |= DISP_PIN_ALL;  // 初始全部 Hi-Z

    disp_digit[0] = 10;
    disp_digit[1] = 10;
    disp_digit[2] = 10;
    disp_dp1 = 0;
    disp_dp2 = 0;
}

void bsp_display188_set_digit(int d1, int d2, int d3, int dp1, int dp2)
{
    if (d1 >= 0 && d1 <= 10) disp_digit[0] = (u8)d1;
    if (d2 >= 0 && d2 <= 10) disp_digit[1] = (u8)d2;
    if (d3 >= 0 && d3 <= 10) disp_digit[2] = (u8)d3;
    disp_dp1 = dp1 ? 1 : 0;
    disp_dp2 = dp2 ? 1 : 0;
}

void bsp_display188_set_temp(int temp_x10)
{
    if (temp_x10 == 0xFFFF || temp_x10 < -990 || temp_x10 > 990) {
        disp_digit[0] = 10; disp_digit[1] = 10; disp_digit[2] = 10;
        disp_dp1 = 1; disp_dp2 = 0;
        return;
    }

    int t_int = (temp_x10 + 5) / 10;
    if (t_int < 0) t_int = -t_int;
    if (t_int > 99) t_int = 99;

    if (t_int >= 10) {
        disp_digit[0] = 10;
        disp_digit[1] = t_int / 10;
        disp_digit[2] = t_int % 10;
    } else {
        disp_digit[0] = 10;
        disp_digit[1] = 10;
        disp_digit[2] = t_int;
    }
    disp_dp1 = 1; disp_dp2 = 0;
}

/**
 * @brief 批量扫描函数
 *
 * 在 func_process() 中调用. 一次调用完成全帧扫描.
 * 每段点亮保持 SEG_HOLD_US (800us) 确保亮度.
 * 蓝牙忙时此函数阻塞约 6-8ms (亮8段) 或 ~18ms (全亮),
 * 但 BLE 射频由独立硬件处理, 软件阻塞不影响连接稳定性.
 *
 * 这不是定时器中断, 不涉及 GIE, 不会触发 CRC 崩溃.
 * delay_us() 是纯 CPU 忙等, 不依赖任何中断.
 */
void bsp_display188_process(void)
{
    batch_scan();
}