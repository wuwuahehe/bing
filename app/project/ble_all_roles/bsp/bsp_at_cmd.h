#ifndef _BSP_AT_CMD_H
#define _BSP_AT_CMD_H


#define BSP_AT_CMD_CM_PAGE(x)      PAGE3(x)
#define BLE_PROFILE_CM_PAGE(x)     PAGE4(x)


#define HSUART_TRANSFER_MAX_LEX     256

//Event Message, 范围：0x700 ~ 0x7ff
typedef enum{
    MSG_AT_CMD_RX_DONE=0xfff0,
    MSG_AT_CMD_TX_DONE,
    MSG_BLE_CONNECTED,
    MSG_BLE_DISCONNECTED,
    MSG_BLE_TX_DATA,
    MSG_BLE_RX_DATA,

}MSG_TYPE_TYPEDEF;


typedef enum{
    BLE_DISC_STATUS=0,
    BLE_CON_STATUS,
}CON_STATUS_TYPEDEF;

typedef enum{
    UUID_ERROR_TYPE=0,
    UUID_16_TYPE,
    UUID_128_TYPE,

}UUID_TYPE_TYPEDEF;

typedef struct {
    uint8_t flash_flag;
    u8 wdt_status;
    u8 role_status;
    u8 echo_status;
    uint16_t con_handle;  // 修改为uint16_t以正确存储BLE连接句柄
    u8 sleep_status;
    u8 enc_status;
    u8 con_notify_status;
    u8 notify_enable_status;
    u8  at_mode;
    u8  btat_status;
    u8 ble_name[32];
    u8 ble_addr[6];
    u32 uart_baud;
    u16 uart_pack_len;
    u32 password;
    char rssi_val;
    char tx_power;
    u16 con_timeout;
    u16  vbat_vol;
    u16 min_con_interval;
    u16 max_con_interval;
    u16 slavery_latency;
    u16 adv_interval;
    u8  scan_rsp_buf[31];
    u16 scan_rsp_len;
    u8 scan_rsp_status;
    u16 device_vdd;
    char device_temp;
    u8 update_con_param_done;

} at_cmd_t;

typedef struct {
    u8 rx_done;
    u16 rx_len;
    u8 tx_done;
    u16 tx_len;
    u16 tmp_len;
    uint8_t rxbuf_tmp[HSUART_TRANSFER_MAX_LEX];
    uint8_t rxbuf_data[HSUART_TRANSFER_MAX_LEX];
    uint8_t txbuf_data[HSUART_TRANSFER_MAX_LEX];


} hsuart_rx_t;

typedef struct {

    u16 ble_rx_len;
    uint8_t ble_rx_data[256];
} ble_data_t;

typedef void (*at_cmd_handler_t)(uint8_t *data, uint16_t len);

typedef struct
{
	void* p_cmd;
	at_cmd_handler_t at_cmd_handler;
}bsp_at_cmd_t;

void bsp_at_cmd_init(void);
void bsp_at_cmd_handler(uint16_t msg);
void bsp_at_cmd_rx_data(uint8_t *buf,uint16_t len);
// 获取at_cmd结构体指针
at_cmd_t* bsp_at_cmd_get(void);
void bsp_at_cmd_send_data(uint8_t *buf,uint16_t len);
#endif // _HSUART_TRANSFER_H
