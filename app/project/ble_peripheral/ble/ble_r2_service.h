#ifndef __BLE_R2_SERVICE_H
#define __BLE_R2_SERVICE_H

/*
 * R2 制冰壶 BLE 协议服务（客户协议《蓝牙协议介绍（App）》）
 *
 * Service 0xFFF0
 *   FFF1  write without response  ← App 下发指令
 *   FFF2  notify                  → 设备上报
 *
 * 帧格式（4 字节）：CMD + 温度编码 + 单位 + CRC8
 *   CRC8: poly 0x07 / init 0x00，仅覆盖 Byte1..Byte2（已用协议示例校验）
 *   温度编码 = 摄氏整数 × 5
 */

/* 指令码 */
#define R2_CMD_SET_TARGET     0x01
#define R2_CMD_QUERY_CUR      0x02
#define R2_CMD_QUERY_TARGET   0x03

/* 应答/上报码 */
#define R2_RPT_REALTIME       0x81
#define R2_RPT_CURRENT        0x82
#define R2_RPT_TARGET         0x83

void ble_r2_service_init(void);
void ble_r2_service_proc(void);      /* 5ms~100ms 周期调用 */

#endif /* __BLE_R2_SERVICE_H */
