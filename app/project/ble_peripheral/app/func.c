#include "include.h"
#include "dec.h"
#include "app_temp_control.h"

func_cb_t func_cb AT(.buf.func_cb);

AT(.text.app.proc.func)
void func_process(void)
{
    WDT_CLR();
#if BSP_DISPLAY188_EN
    // ★ 查理复用单段扫描: 每次调用点亮1个LED段, 然后立即返回
    //   func_process 由蓝牙协议栈以 ~1kHz 轮询调用,
    //   熄灭段快速跳过, 亮段点亮后等待下次调用切换.
    //   23段扫描一轮约需 2-3ms (高频循环).
    //   不使用硬件定时器中断(会与BLE协议栈冲突崩溃).
    bsp_display188_process();
#endif
#if BSP_SARADC_EN
    bsp_saradc_process();          // NTC采样(含VBAT/温度传感器)
#endif

    /* === 应用层温度控制 (每100ms调用一次, 由func_process轮询频率决定) === */
    {
        static u32 app_tick = 0;
        if (tick_check_expire(app_tick, 100)) {
            app_tick = tick_get();
            app_temp_control_process();
        }
    }

    /* === 设置模式闪烁 (每500ms翻转一次) === */
    {
        static u32 flash_tick = 0;
        if (tick_check_expire(flash_tick, 500)) {
            flash_tick = tick_get();
            extern void app_temp_control_flash_toggle(void);
            app_temp_control_flash_toggle();
        }
    }

#if BSP_CHARGE_EN
    bsp_charge_process();
#endif

#if BSP_TSEN_EN
    bsp_tsensor_process();
#endif // BSP_TSEN_EN

    prod_test_process();

#if (TONE_PLAY_EN && !TONE_PLAY_THREAD_EN)
    bsp_tone_play_process();
#endif
}

//func common message process
AT(.text.func.msg)
void func_message(u16 msg)
{
    bool event_valid = true;

    switch (msg) {

#if BSP_KEY_EN
        case KEY_ID_PP | KEY_SHORT_UP:      // PP短按
            app_temp_control_on_pp_short();
            break;

        case KEY_ID_PP | KEY_LONG:          // PP长按(3秒触发)
            app_temp_control_on_pp_long();
            break;

        case KEY_ID_PP | KEY_HOLD:          // PP连按(忽略)
            printf("PP key hold\n");
            break;

        case KEY_ID_PP | KEY_LONG_UP:       // PP长按抬起(忽略)
            printf("PP key long up\n");
            break;

        case KEY_ID_PP | KEY_DOUBLE:        // PP双击
            printf("PP key double down\n");
            break;

        case KEY_ID_PP | KEY_THREE:         // PP三击
            printf("PP key triple down\n");
            break;

        case KEY_ID_K1 | KEY_SHORT_UP:
            break;

        case KEY_ID_K2 | KEY_SHORT_UP:
            break;

        case KEY_ID_K3 | KEY_SHORT_UP:
            break;

        case KEY_ID_K4 | KEY_SHORT_UP:
            break;
#endif

        default:
            event_valid = false;
            break;
    }

    if (event_valid) {
        lowpwr_pwroff_delay_reset();
        lowpwr_sleep_delay_reset();
    }
}

///进入一个功能的总入口
AT(.text.func)
void func_enter(void)
{
    bsp_param_sync();
    lowpwr_sleep_delay_reset();
    lowpwr_pwroff_delay_reset();
}

AT(.text.func)
void func_exit(void)
{
    func_cb.sta = FUNC_IDLE;
}

AT(.text.func)
void func_run(void)
{
    printf("%s\n", __func__);

    while (1) {
        func_enter();
        switch (func_cb.sta) {
#if FUNC_BT_EN
        case FUNC_BT:
            func_bt();
            break;
#endif
#if FUNC_LE_FCC_EN
        case FUNC_LE_FCC:
            func_le_fcc();
            break;
#endif
#if FUNC_LE_BQB_RF_EN
        case FUNC_LE_BQB_RF:
            func_le_bqb_rf();
            break;
#endif
#if FUNC_IODM_EN
        case FUNC_IODM:
            func_iodm();
            break;
#endif
#if FUNC_TBOX_EN
        case FUNC_TBOX:
            func_tbox();
            break;
#endif
#if FUNC_IDLE_EN
        case FUNC_IDLE:
            func_idle();
            break;
#endif
        case FUNC_PWROFF:
            func_pwroff();
            break;
        default:
            func_exit();
            break;
        }
    }
}