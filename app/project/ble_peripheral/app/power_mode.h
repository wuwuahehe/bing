#ifndef _POWER_MODE_H
#define _POWER_MODE_H

/*
 * 电源模式管理（PRD FR-MODE-01 / FR-PWR-06）
 *
 * 插电检测不占用 GPIO：直接用 SoC 片内充电器的 DC 检测
 *   charge_detect_dc()  -> CHARGE_DC_STATUS_OFF / ONLINE_BUT_ERR / ONLINE
 *   charge_get_status() -> 充电阶段（涓流/恒流/恒压/充满）
 */

typedef enum {
    PWR_INIT = 0,   /* 上电，尚未判定 */
    PWR_CHARGING,   /* 插电，充电中 */
    PWR_DIRECT,     /* 插电，已充满 -> 适配器直供 */
    PWR_BATTERY,    /* 未插电，电池供电（禁止制冷/风扇） */
    PWR_OFF,        /* 低电关机 */
} pwr_mode_t;

void       power_mode_init(void);

/* 10ms 周期调用：去抖、状态迁移、执行器动作 */
void       power_mode_process(void);

pwr_mode_t power_mode_get(void);

/* 是否允许 TEC / 风扇动作：仅插电（充电中或直供）时允许 */
bool       power_mode_allow_cool(void);

/* 当前是否处于充电中（用于单位段 1Hz 闪烁） */
bool       power_mode_is_charging(void);

/* 电池电压（mV，5 点平均） */
u16        power_mode_get_vbat(void);

/* 粗略电量百分比（0-100，仅供调试参考） */
u8         power_mode_get_soc(void);

/* 去抖后的插电状态（1=插着） */
bool       power_mode_dc_online(void);

#endif /* _POWER_MODE_H */
