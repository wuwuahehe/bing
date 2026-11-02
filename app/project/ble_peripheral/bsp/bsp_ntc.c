#include "include.h"
#include "driver_saradc.h"
#include "driver_gpio.h"
#include "bsp_ntc.h"
#define BSP_NTC_EN                      1  
#if BSP_NTC_EN

static uint16_t g_ntc_voltage_mv = 0;       // NTC+引脚电压(mV)
static int g_ntc_temperature = 0x7FFF;       // 温度值(℃×10)
static bool g_ntc_data_valid = false;        // 数据有效性
static uint32_t g_ntc_last_tick = 0;         // 上次采样时间戳

/* ==================== 滑动中值滤波 ==================== */
#define NTC_MEDIAN_WINDOW       5       // 中值滤波窗口大小(奇数)
static int  g_ntc_filter_buf[NTC_MEDIAN_WINDOW];  // 滤波缓冲区
static u8   g_ntc_filter_idx = 0;                 // 当前写入索引
static u8   g_ntc_filter_filled = 0;              // 缓冲是否填满

/**
 * @brief 滑动中值滤波 (冒泡排序取中值)
 *        用于抑制NTC采样噪声, 避免TEC在临界点频繁切换
 *
 * @param new_value 新采样值(℃×10)
 * @return 滤波后的温度值(℃×10)
 *
 * 【修复9】NTC采样噪声滤波:
 *   采用5点滑动中值滤波, 有效消除ADC单次跳变
 *   同时在app_temp_control层增加HYSTERESIS_DEBOUNCE_CNT消抖
 *   两层防护确保TEC不会因噪声反复切换
 */
static int ntc_median_filter(int new_value)
{
    uint8_t i, j;
    int temp;
    int sort_buf[NTC_MEDIAN_WINDOW];

    // 1. 写入环形缓冲区
    g_ntc_filter_buf[g_ntc_filter_idx] = new_value;
    g_ntc_filter_idx = (g_ntc_filter_idx + 1) % NTC_MEDIAN_WINDOW;
    if (!g_ntc_filter_filled && g_ntc_filter_idx == 0) {
        g_ntc_filter_filled = 1;
    }

    // 2. 缓冲区未填满时直接返回原始值
    if (!g_ntc_filter_filled) {
        return new_value;
    }

    // 3. 复制缓冲区并排序(冒泡, N很小)
    for (i = 0; i < NTC_MEDIAN_WINDOW; i++) {
        sort_buf[i] = g_ntc_filter_buf[i];
    }
    for (i = 0; i < NTC_MEDIAN_WINDOW - 1; i++) {
        for (j = 0; j < NTC_MEDIAN_WINDOW - 1 - i; j++) {
            if (sort_buf[j] > sort_buf[j + 1]) {
                temp = sort_buf[j];
                sort_buf[j] = sort_buf[j + 1];
                sort_buf[j + 1] = temp;
            }
        }
    }

    // 返回中值(索引2)
    return sort_buf[NTC_MEDIAN_WINDOW / 2];
}

/**
 * @brief 通过NTC阻值查表计算温度
 *        基于10K赂赂 NTC (B=3950) 的典型R-T特性查表+线性插值
 *        温度范围 -20掳C ~ 100掳C，步进5掳C
 *
 * @param rntc_ohm NTC当前阻值（赂赂）
 * @return 温度值（掳C 脳 10），例如25.3掳C返回253
 */
static int ntc_calc_temperature(uint32_t rntc_ohm)
{
    /* 10K赂赂 NTC (B=3950) 阻值-温度对应表 */
    /* 10K NTC, B=3435 table (regenerated 2026-11-02) */
    static const uint32_t r_table[] = {
        77523,  /* -20掳C */
        59606,  /* -15掳C */
        46290,  /* -10掳C */
        36290,  /* -5掳C  */
        28704,  /*  0掳C  */
        22897,  /*  5掳C  */
        18410,  /* 10掳C  */
        14916,  /* 15掳C  */
        12171,  /* 20掳C  */
        10000,  /* 25掳C  */
        8269,   /* 30掳C  */
        6881,   /* 35掳C  */
        5759,   /* 40掳C  */
        4847,   /* 45掳C  */
        4101,   /* 50掳C  */
        3488,   /* 55掳C  */
        2981,   /* 60掳C  */
        2559,   /* 65掳C  */
        2207,   /* 70掳C  */
        1912,   /* 75掳C  */
        1662,   /* 80掳C  */
        1451,   /* 85掳C  */
        1272,    /* 90掳C  */
        1118,    /* 95掳C  */
        987,    /* 100掳C */
    };
    #define R_TABLE_MIN_TEMP     (-20)
    #define R_TABLE_MAX_TEMP     (100)
    #define R_TABLE_STEP         (5)
    #define R_TABLE_SIZE         (sizeof(r_table) / sizeof(r_table[0]))

    uint8_t i;
    int32_t temp_x10;

    if (rntc_ohm == 0) {
        return R_TABLE_MAX_TEMP * 10;  // 短路按最高温处理
    }

    /* 边界检查 */
    if (rntc_ohm >= r_table[0]) {
        temp_x10 = R_TABLE_MIN_TEMP * 10;
    } else if (rntc_ohm <= r_table[R_TABLE_SIZE - 1]) {
        temp_x10 = R_TABLE_MAX_TEMP * 10;
    } else {
        /* 查表线性插值 */
        for (i = 0; i < R_TABLE_SIZE - 1; i++) {
            if (rntc_ohm <= r_table[i] && rntc_ohm >= r_table[i + 1]) {
                int32_t temp_low  = (R_TABLE_MIN_TEMP + i * R_TABLE_STEP) * 10;
                int32_t temp_high = (R_TABLE_MIN_TEMP + (i + 1) * R_TABLE_STEP) * 10;
                uint32_t r_low  = r_table[i];
                uint32_t r_high = r_table[i + 1];
                temp_x10 = temp_low + (int32_t)((uint32_t)(temp_high - temp_low) * (r_low - rntc_ohm) / (r_low - r_high));
                break;
            }
        }
    }

    return temp_x10;
}

/**
 * @brief 初始化NTC温度检测
 *        1. 配置PA6为模拟输入
 *        2. 初始化SARADC PA6通道 + VRTC参考通道
 */
void bsp_ntc_init(void)
{
    gpio_init_typedef gpio_config;
    saradc_channel_init_typedef saradc_channel_init_struct;

    /* Step 1: PA6配置为模拟输入 */
    gpio_config.gpio_pin   = GPIO_PIN_6;
    gpio_config.gpio_dir   = GPIO_DIR_INPUT;
    gpio_config.gpio_mode  = GPIO_MODE_ANALOG;
    gpio_config.gpio_fen   = GPIO_FEN_GPIO;
    gpio_config.gpio_fdir  = GPIO_FDIR_SELF;
    gpio_config.gpio_pupd  = GPIO_PUPD_NULL;
    gpio_config.gpio_drv   = GPIO_DRV_6MA;
    gpio_init(GPIOA_REG, &gpio_config);

    /* Step 2: 初始化SARADC通道 */
    saradc_channel_init_struct.channel       = NTC_ADC_CHANNEL | NTC_REF_VOLTAGE_CHANNEL;
    saradc_channel_init_struct.pullup_en     = SARADC_PULLUP_DIS;    // 外部已有R12上拉
    saradc_channel_init_struct.setup_time    = SARADC_ST_8_CLK;
    saradc_channel_init_struct.interrupt_en  = DISABLE;
    saradc_channel_init(&saradc_channel_init_struct);

    /* 首次采样 */
    bsp_ntc_process();

    printf("[NTC] Initialized on PA6\n");
}

/**
 * @brief 执行NTC采样与温度计算
 *        流程：采集ADC -> 计算电压 -> 反算阻值 -> 查表得温度
 */
static void bsp_ntc_sample_and_calc(void)
{
    uint32_t i;
    uint32_t ntc_adc_sum = 0;
    uint32_t ref_adc_sum = 0;
    uint32_t ntc_adc_avg, ref_adc_avg;
    uint32_t ntc_voltage;
    uint32_t rntc;
    uint32_t vddio_mv;
    int temperature;

    /* Step 1: 多次采样取平均 */
    for (i = 0; i < NTC_SAMPLE_LOOP; i++) {
        saradc_kick_start(NTC_ADC_CHANNEL | NTC_REF_VOLTAGE_CHANNEL);
        while (!(saradc_get_channel_flag(NTC_ADC_CHANNEL) &&
                 saradc_get_channel_flag(NTC_REF_VOLTAGE_CHANNEL)));

        ntc_adc_sum += saradc_get_data(NTC_ADC_CHANNEL);
        ref_adc_sum += saradc_get_data(NTC_REF_VOLTAGE_CHANNEL);
    }

    /* Step 2: 平均ADC值 */
    ntc_adc_avg = ntc_adc_sum / NTC_SAMPLE_LOOP;
    ref_adc_avg = ref_adc_sum / NTC_SAMPLE_LOOP;
    printf("ntc_adc_avg = %d, ref_adc_avg = %d\n", ntc_adc_avg, ref_adc_avg);

    /* Step 3: 计算NTC+引脚实际电压(mV) */
    ntc_voltage = NTC_REF_VOLTAGE_VALUE * ntc_adc_avg / ref_adc_avg;
    g_ntc_voltage_mv = (uint16_t)ntc_voltage;
    printf("ntc_voltage = %dmV\n", ntc_voltage);

    /* Step 4: 获取VDDIO实际电压 */
    {
        uint8_t vddio_level = pmu_get_vddio();
        vddio_mv = 2400 + (uint32_t)vddio_level * 100;
    }

    /* Step 5: 根据分压公式计算NTC阻值
     *   VNTC+ = VDDIO * R_NTC / (R12 + R_NTC)
     *   => R_NTC = R12 * VNTC+ / (VDDIO - VNTC+)
     */
    if (ntc_voltage >= vddio_mv - 10) {
        rntc = 200000;  // 接近VDDIO，阻值极大（开路/低温）
    } else if (ntc_voltage < 10) {
        rntc = 100;     // 接近0，阻值极小（短路/高温）
    } else {
        rntc = (uint32_t)NTC_PULLUP_RESISTOR * ntc_voltage / (vddio_mv - ntc_voltage);
    }
    printf("rntc = %d\n", rntc);

    /* Step 6: 查表计算温度 */
    temperature = ntc_calc_temperature(rntc);
    printf("temperature(filter) = %d\n", temperature);

    /* Step 7: 滑动中值滤波 → 抑制ADC噪声 */
    g_ntc_temperature = ntc_median_filter(temperature);
    g_ntc_data_valid = true;
}

uint16_t bsp_ntc_get_voltage(void)
{
    return g_ntc_voltage_mv;
}

int bsp_ntc_get_temperature(void)
{
    if (!g_ntc_data_valid) {
        return 0xFFFF;
    }
    return g_ntc_temperature;
}

void bsp_ntc_process(void)
{
    if (tick_check_expire(g_ntc_last_tick, NTC_SAMPLE_INTERVAL_MS)) {
        bsp_ntc_sample_and_calc();
        g_ntc_last_tick = tick_get();
    }
}

#endif // BSP_NTC_EN
