#include "include.h"
#include "driver_charge.h"
#include "bsp_charge.h"
#include "bsp_saradc_vbat.h"


#if BSP_CHARGE_EN

#define RECHARGE_THRESHOLD                      (4050)      //4.05V, avoid micro charge cycles

const char charge_status_info[][29] = {
    "[charge] uninitialized\n",
    "[charge] vusb disconnected\n",
    "[charge] finish-but-connect\n",
    "[charge] trickle cur state\n",
    "[charge] const cur state\n",
    "[charge] const vol state\n",
};

// start charge -> [trigle stage] -> constant current & constant voltage -> finish charge
void bsp_charge_init(void)
{
    charge_init_typedef charge_init_struct;

    charge_init_struct.const_curr = CHARGE_CUR_20mA;      // 0.5C for 40mAh (501012)
    charge_init_struct.cutoff_curr = CHARGE_CUTOFF_CUR_2mA5; // 0.06C
    charge_init_struct.cutoff_volt = CHARGE_CUTOFF_VOL_4V2;  // cell FC = 4.2V
    charge_init_struct.dcin_reset = CHARGE_DCIN_RESET_DIS;
    charge_init_struct.trick_curr = CHARGE_CUR_5mA;       // trickle 0.125C
    charge_init_struct.trick_curr_en = ENABLE;
    charge_init_struct.mode = CHARGE_MODE_FULL_DISCONNECT;

    charge_init(&charge_init_struct);
}

// 测试函数
AT(.text.app.proc.charge)
void bsp_charge_process(void)
{
    static u32 delay_cnt1;
    static u32 delay_cnt2;
    static u8 charge_status_last = CHARGE_STA_OFF;
    u8 charge_status = charge_status_last;

    charge_status = charge_get_status();
    if (charge_status != charge_status_last) {
        charge_status_last = charge_status;
        printf(charge_status_info[charge_status]);
    }

    if (charge_status_last > CHARGE_STA_OFF_BUT_DC_IN) {
        lowpwr_pwroff_delay_reset();
        lowpwr_sleep_delay_reset();
    }

    if (tick_check_expire(delay_cnt1, 100)) {
        delay_cnt1 = tick_get();
        charge_process();

        /*if stop charging by battery fully charged, we do recharging here*/
        if ((charge_get_status() == CHARGE_STA_OFF_BUT_DC_IN) && (charge_get_mode() != CHARGE_MODE_FULL_KEEP)) {
            #if BSP_VBAT_DETECT_EN
                uint32_t vbat_cur = bsp_get_vbat_level(1);

                if(vbat_cur < RECHARGE_THRESHOLD) {
                    printf("[charge] start re-charging\n");
                    charge_status_update(CHARGE_STA_ON_CON_CURR);
                }
            #endif
        }
    }

    if (tick_check_expire(delay_cnt2, 2000)) {
        delay_cnt2 = tick_get();
        if (charge_detect_dc() == CHARGE_DC_STATUS_ONLINE_BUT_ERR) {
            printf("[charge] CHARGE LINE VOLTAGE ERROR!!!\n");
        }
    }
}


/* ==================== TEC / 充电互斥 ==================== */
static bool charge_active = true;

/*
 * TEC 开着时暂停充电：
 *   原因：无 power path，TEC 2.1A 会把 5V 拉到限流，
 *         导致充电器反复报 vusb disconnected / LINE VOLTAGE ERROR。
 *   做法：charge_deinit() 只清 RTCCON8 一位，不影响
 *         charge_detect_dc()（读 RTCCON），插拔电检测仍然正常。
 */
void bsp_charge_set_active(bool active)
{
    if (active == charge_active) {
        return;
    }
    charge_active = active;
    if (active) {
        bsp_charge_init();
        printf("[charge] resume (TEC off)\r\n");
    } else {
        charge_deinit();
        printf("[charge] suspend (TEC on)\r\n");
    }
}

#endif // BSP_CHARGE_EN
