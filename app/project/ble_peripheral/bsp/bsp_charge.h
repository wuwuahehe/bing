#ifndef _BSP_CHARGE_H
#define _BSP_CHARGE_H


void bsp_charge_init(void);
void bsp_charge_process(void);



/* TEC 与充电互斥：active=false 时暂停充电以保证制冷电流 */
void bsp_charge_set_active(bool active);

#endif // _BSP_CHARGE_H
