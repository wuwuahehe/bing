#include "include.h"
#include "driver_com.h"
#include "driver_hsuart.h"
#include "driver_gpio.h"
#include "bsp_at_cmd.h"
#include "api_btstack.h"
#include "hsuart_transfer.h"
#include "ble_client.h"


#define AT_CMD_VERSION   "VER:BF_V2.2.0\r\n"

#define TRACE_EN                1
#if TRACE_EN
#define TRACE(...)              printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif



static hsuart_rx_t hsuart_cb AT(.buf.hsuart_cb);
static uint8_t  at_profile_data[250] AT(.buf.at_profile);
static at_cmd_t at_cmd_cb AT(.buf.at_cmd_cb);
static ble_data_t  ble_data_cb AT(.buf.ble_data_cb);

/*
十进制数字格式的字符串（比如 "115200"、"32"、"123456"）转换成程序能直接使用的无符号 32 位整数（uint32_t）。
*/
uint32_t string_to_uint_decimal(const uint8_t *str) {

    if (str == NULL) return 0;
    TRACE("str_conv = %s,\n", str);

    uint32_t result = 0;
    // 判断十六进制前缀
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        result = (uint32_t)strtoul((const char*)str, NULL, 16);
    }
    else
    {
        // 转换字符为数字
        // 核心转换逻辑：
        // - result * 10：把已转换的数字左移一位（比如 1 → 10，12 → 120）
        // - (*str - '0')：把字符'0'-'9'转为对应的数字（比如 '5'-'0'=5）
        while (*str != '\0') {
            result = result * 10 + (*str - '0');
            str++;
        }
    }
    TRACE("result = %d,\n", result);
    return result;
}

int string_to_int_decimal(const uint8_t *str)
{
   int result = 0;
   int negative = 0;
    // 判断正负号
    if (*str == '-') {
        negative = 1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    // 转换字符为数字
    while (*str != '\0') {
        result = result * 10 + (*str - '0');
        str++;
    }
    // 取反
    if (negative) {
        result = -result;
    }
    return result;
}






int string_to_hex(char *str, uint8_t *out, uint16_t *outlen)
{
    char *p = str;
    char high = 0, low = 0;
    int tmplen = strlen(p), cnt = 0;
    tmplen = strlen(p);
    while(cnt < (tmplen / 2))
    {
        high = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;
		low = (*(++ p) > '9' && ((*p <= 'F') || (*p <= 'f'))) ? *(p) - 48 - 7 : *(p) - 48;
        out[cnt] = ((high & 0x0fU) << 4 | (low & 0x0fU)) & 0xffU;
        p ++;
        cnt ++;
    }
    if(tmplen % 2 != 0) out[cnt] = ((*p > '9') && ((*p <= 'F') || (*p <= 'f'))) ? *p - 48 - 7 : *p - 48;

    if(outlen != NULL) *outlen = tmplen / 2 + tmplen % 2;
    return tmplen / 2 + tmplen % 2;
}

static void at_cmd_param_save_flash(void)
{
    cm_write(&at_cmd_cb,BSP_AT_CMD_CM_PAGE(0),sizeof(at_cmd_cb));
    cm_sync();
}

static void at_profile_data_save_flash(void)
{
    const u8 *profile_data=ble_get_profile_data();  // 修改为const指针
    cm_write((u8*)profile_data,BLE_PROFILE_CM_PAGE(0),250);  // 强制类型转换
    cm_sync();
}

static uint16_t at_profile_data_len_get(void)
{
     const u8 *profile_data=ble_get_profile_data();  // 修改为const指针
     uint16_t profile_len=0;
     while(1){
          if(profile_data[profile_len]==0x00&&profile_data[profile_len+1]==0x00){
               break;
          }
          profile_len=profile_len+profile_data[profile_len];
     }
     return  profile_len+2;
}


uint8_t *service_uuid_get(uint8_t *uuid_type,uint8_t service_idx)
{

    uint16_t data_len=0;
    uint8_t srv_index=0;
    const uint8_t *p_uuid=NULL;
    const u8 *profile_data=ble_get_profile_data();
    uint16_t profile_len=at_profile_data_len_get();


   for(uint16_t i=0;i<profile_len;i++){
        if(profile_data[i]==0x00&&profile_data[i+1]==0x28){
          if(srv_index==service_idx){
                p_uuid=&profile_data[i+2];
                data_len=GET_LE16(&profile_data[i-6]);
                break;
          }
          srv_index=srv_index+1;
        }
    }


    if(data_len==24){
        *uuid_type= UUID_128_TYPE;
        return (uint8_t*)p_uuid;  // 强制类型转换解决const警告
     }else  if(data_len==10){

        *uuid_type= UUID_16_TYPE;
        return (uint8_t*)p_uuid;  // 强制类型转换解决const警告
    }
    *uuid_type= UUID_ERROR_TYPE;

    return NULL;
}


uint8_t *charac_uuid_get(uint8_t service_idx, uint8_t charac_idx)
{
    uint8_t srv_uuid_type=0;
    uint8_t chara_tmp_index=0;
    uint8_t *profile_data= service_uuid_get(&srv_uuid_type, service_idx);
    uint8_t *p_uuid=NULL;
    uint16_t profile_len=at_profile_data_len_get();

    for(uint16_t i=0;i<profile_len;i++){
        if(profile_data[i]==0x03&&profile_data[i+1]==0x28){
          if(chara_tmp_index==charac_idx){
                p_uuid=&profile_data[i+5];
                //data_len=GET_LE16(&profile[i-6]);
                //break;
                return  p_uuid;
          }
          chara_tmp_index=chara_tmp_index+1;
        }
    }

    return NULL;

}





#if 0//1


u32 ble_get_scan_data(u8 *scan_buf, u32 buf_size)
{
    memset(scan_buf, 0, buf_size);
    uint8_t scan_rsp[31] = {
    // Flags general discoverable, BR/EDR not supported
       0x02, 0x01, 0x06,
    // Manufacturer Specific Data
       0x03, 0xff, 0x42, 0x06,
    };

    at_cmd_t  *at_cmd  =&at_cmd_cb;
    if(at_cmd->scan_rsp_status==DISABLE||at_cmd->scan_rsp_len==0){
        memcpy(scan_buf,scan_rsp,7);
        return 7;
    }

    printf("at_cmd->scan_rsp_len = %d\n",at_cmd->scan_rsp_len);
    print_r(at_cmd->scan_rsp_buf,at_cmd->scan_rsp_len);

    scan_rsp[3]=at_cmd->scan_rsp_len+1;
    scan_rsp[4]=0xFF;
    memcpy(&scan_rsp[5], at_cmd->scan_rsp_buf,at_cmd->scan_rsp_len);

    print_r(scan_rsp,sizeof(scan_rsp));
    memcpy(scan_buf,scan_rsp,at_cmd->scan_rsp_len+5);
    return (at_cmd->scan_rsp_len+5);
}



void ble_sm_param_init(void)
{
    at_cmd_t  *at_cmd  =&at_cmd_cb;
    if( at_cmd->password==0){
        uint8_t security_req_en =0;
        uint8_t auth_req = SM_AUTHREQ_BONDING | SM_AUTHREQ_MITM_PROTECTION;
        uint8_t io_capability = IO_CAPABILITY_NO_INPUT_NO_OUTPUT;
        ble_sm_init(security_req_en, io_capability, auth_req);

    }
    else{
        uint8_t security_req_en =1;
        uint8_t auth_req = SM_AUTHREQ_BONDING | SM_AUTHREQ_MITM_PROTECTION;
        uint8_t io_capability = IO_CAPABILITY_DISPLAY_ONLY;
        ble_sm_init(security_req_en, io_capability, auth_req);
    }

}


void ble_adv_param_init(void)
{
    adv_param_t adv_info;
    at_cmd_t  *at_cmd  =&at_cmd_cb;
    memset(&adv_info,0,sizeof(adv_info));
    adv_info.adv_len = ble_get_adv_data(adv_info.adv_data, sizeof(adv_info.adv_data));
    adv_info.scan_rsp_len = ble_get_scan_data(adv_info.scan_rsp, sizeof(adv_info.scan_rsp));
    adv_info.adv_int_min = adv_info.adv_int_max = at_cmd->adv_interval;
    adv_info.adv_type = ADV_TYPE_IND;
    adv_info.adv_en = 1;
    adv_info.channel_map = ADV_ALL_CHNLS_EN;

    ble_set_adv_param(&adv_info);
}


#endif // 0
AT(.com_periph.cmd.cmd_rx_data)
void bsp_at_cmd_rx_data(uint8_t *buf,uint16_t len)
{
    hsuart_rx_t *hsuart=&hsuart_cb;
    hsuart->rx_len=len;
    hsuart->tmp_len=len;
    memcpy(hsuart->rxbuf_data,buf,hsuart->rx_len);
    memcpy(hsuart->rxbuf_tmp,buf,hsuart->tmp_len);
    msg_enqueue(MSG_AT_CMD_RX_DONE);
}


void bsp_at_cmd_send_data(uint8_t *buf,uint16_t len)
{
     hsuart_dma_tx(buf,len);
}

void ble_data_send_for_uart(uint8_t *buf,uint16_t len)
{
    ble_data_t *ble_data= &ble_data_cb;
    ble_data->ble_rx_len=len;
    memcpy(ble_data->ble_rx_data,buf,ble_data->ble_rx_len);
    msg_enqueue(MSG_BLE_TX_DATA);
    printf("-->MSG_BLE_TX_DATA\n");
}


static void bsp_at_cmd_test(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_test\n");
    uint8_t tx_buf[]="OK\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}


static void bsp_at_cmd_name_get(uint8_t *data, uint16_t len)
{
  TRACE("bsp_at_cmd_name_get->ok\n");
  uint8_t tx_buf[]="+NAME:";
  bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
  bsp_at_cmd_send_data((uint8_t*)xcfg_cb.le_name, strlen(xcfg_cb.le_name));
  uint8_t ack_buf[]="\r\n,OK\r\n";
  bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}





static void ble_adv_param_set_name(void)
{
    adv_param_t adv_info;

    memset(&adv_info,0,sizeof(adv_info));
    adv_info.adv_len = ble_get_adv_data(adv_info.adv_data, sizeof(adv_info.adv_data));
    adv_info.scan_rsp_len = ble_get_scan_data(adv_info.scan_rsp, sizeof(adv_info.scan_rsp));
    adv_info.adv_int_min = adv_info.adv_int_max = 800;
    adv_info.adv_type = ADV_TYPE_IND;
    adv_info.adv_en = 1;
    adv_info.channel_map = ADV_ALL_CHNLS_EN;

    ble_set_adv_param(&adv_info);
}

static void bsp_at_cmd_name_set(uint8_t *data, uint16_t len)
{
    TRACE("bsp_at_cmd_name_set->ok\n");
    if(len==0||len>12){
        return ;
   }
   at_cmd_t  *at_cmd  =&at_cmd_cb;
   memset(at_cmd->ble_name,0,sizeof(at_cmd->ble_name));
   memcpy(at_cmd->ble_name,data,len);
   memset(xcfg_cb.le_name,0,sizeof(xcfg_cb.le_name));
   memcpy(xcfg_cb.le_name,data,len);
   ble_adv_param_set_name();
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    at_cmd_param_save_flash();
}

static void bsp_at_cmd_baud_get(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_baud_get\n");
    at_cmd_t *at_cmd=&at_cmd_cb;
    char test_buf[32]={0};
    sprintf(test_buf,"+UART:%d", (int)at_cmd->uart_baud);
    bsp_at_cmd_send_data((uint8_t*)test_buf, strlen(test_buf));
    uint8_t tx_buf[]="\r\n, OK\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));

}

static void bsp_at_cmd_baud_set(uint8_t *data, uint16_t len)
{
/*
指令描述：设置波特率
• 指令类型：写入
• 指令代码：AT+UART=X\r\n
• 支持参数：无
• 设置/响应：OK\r\n
• 备注：X值可为2400、9600、19200或115200（默认值）
*/
   if(len<4){
       return;
   }
     printf("-->bsp_at_cmd_baud_set\n");
     at_cmd_t    *at_cmd=&at_cmd_cb;
     uint32_t baud  =string_to_uint_decimal(data);

   if(baud==2400||baud==9600||baud==19200||baud==115200){
       at_cmd->uart_baud=baud;
       uint8_t tx_buf[]="OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       hsuart_baud_config(at_cmd->uart_baud);
        at_cmd_param_save_flash();
   }
}



static void bsp_at_cmd_ble_disc(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_disc\n");
   uint8_t tx_buf[]="OK\r\n";
   at_cmd_t *at_cmd=&at_cmd_cb;

   if(at_cmd->con_handle==0){
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       return ;
   }

   ble_disconnect(at_cmd->con_handle);

   for(uint8_t i=0;i<100;i++){
        delay_5ms(1);
        WDT_CLR();
        if(at_cmd->con_handle==0){
           bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
           break;
        }
   }

}


static void bsp_at_cmd_soft_version_get(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_soft_version_get\n");
    uint8_t tx_buf[]= AT_CMD_VERSION;
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}


static void bsp_at_cmd_soft_reset(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_soft_reset\n");
    uint8_t tx_buf[]="SUCCESS\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    sw_reset_kick(13);
}

static void bsp_at_cmd_echo_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_echo_get\n");

   at_cmd_t *at_cmd=&at_cmd_cb;
   if(at_cmd->echo_status){
       uint8_t tx_buf[]="+ECHO:1\r\n, OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }
   else{
      uint8_t tx_buf[]="+ECHO:0\r\n, OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }

}

static void bsp_at_cmd_echo_set(uint8_t *data, uint16_t len)
{
  TRACE("-->bsp_at_cmd_echo_set\n");
  if(len==0){
     return;
  }
  at_cmd_t *at_cmd=&at_cmd_cb;
  uint8_t echo_status=string_to_uint_decimal(data);
  if(echo_status){
    at_cmd->echo_status=ENABLE;
  }
  else{
    at_cmd->echo_status=DISABLE;
  }
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    at_cmd_param_save_flash();
}
static void bsp_at_cmd_role_check(uint8_t *data, uint16_t len)
{
 /*
查询模块工作模式
指令描述：查询模块的主从模式
• 指令类型：读取
• 指令代码：AT+ROLE?
• 支持参数：无
• 设置/响应：Slave\r\n
• 备注：可为Master或Slave（本产品始终为从模式）
 */
   TRACE("-->bsp_at_cmd_role_check\n");
  at_cmd_t *at_cmd=&at_cmd_cb;
  if(at_cmd->role_status==ROLE_MASTER){
      uint8_t tx_buf[]="Master\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
  }
  else if(at_cmd->role_status==ROLE_SLAVE){
       uint8_t tx_buf[]="Slave\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
  }
  else
  {
        uint8_t tx_buf[]="ROLE_UNKNOWN, Waiting for connection\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
  }

}

static void bsp_at_cmd_mode_check(uint8_t *data, uint16_t len)
{

  /*
• 设置/响应：MODE:X\r\n, OK\r\n
• 备注：X值可为0、1或2
+ MODE:1，蓝牙连接时AT指令有效且指令透明传输
+ MODE:2，蓝牙连接时指令无效，所有数据透明传输
+ MODE:0，蓝牙连接时AT指令通过AT_EN引脚配置有效；
  */
    TRACE("-->bsp_at_cmd_mode_check\n");

   uint8_t tx_buf[32]={0};
   at_cmd_t *at_cmd=&at_cmd_cb;
   sprintf((char*)tx_buf,"MODE:%d",at_cmd->at_mode);
    bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}



static void bsp_at_cmd_sleep_check(uint8_t *data, uint16_t len)
{
  /*
指令描述：查询模块睡眠配置参数
• 指令类型：读取
• 指令代码：AT+SLEEP?
• 支持参数：无
• 设置/响应：SLEEP:X\r\n, OK\r\n
• 备注：X值可为0或1，
+ SLEEP=0，禁止芯片进入睡眠。
+ SLEEP=1，可通过SLEEP引脚控制芯片睡眠。
默认为0（不应更改）。
  */
   TRACE("-->bsp_at_cmd_sleep_check\n");
   at_cmd_t    *at_cmd=&at_cmd_cb;
   if(at_cmd->sleep_status==ENABLE){
       uint8_t tx_buf[]="SLEEP:1\r\n, OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }
   else{
       uint8_t tx_buf[]="SLEEP:0\r\n, OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }


}


static void bsp_at_cmd_wdt_status_get(uint8_t *data, uint16_t len)
{
/*5.0.9 查询看门狗状态
指令描述：查询模块软件看门狗状态
• 指令类型：读取
• 指令代码：AT+WDG?
• 支持参数：无
• 设置/响应：WDG:X\r\n, OK\r\n
• 备注：X值可为0或1，
+ WDG=0，看门狗关闭。
+ WDG=1，看门狗开启（默认值）*/
   TRACE("-->bsp_at_cmd_wdt_status_get\n");
   at_cmd_t    *at_cmd=&at_cmd_cb;
   if(at_cmd->wdt_status==ENABLE){
      uint8_t tx_buf[]="WDG:1\r\n, OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }
   else{
      uint8_t tx_buf[]="WDG:0\r\n, OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }

}


static void bsp_at_cmd_connotify_get(uint8_t *data, uint16_t len)
{
/*

指令描述：查询连接状态通知
• 指令类型：读取
• 指令代码：AT+CONNOTIFY?
• 支持参数：无
• 设置/响应：CONNOTIFY:X\r\n, OK\r\n
• 备注：X值可为0或1，
+ CONNOTIFY=0，0：连接状态通知关闭。
+ CONNOTIFY=1，0：连接状态通知开启（默认值）
• 备注：当蓝牙连接状态改变时，默认会有提示，包括"CONNECT OK"、"CONNECT FAIL"、"DISCONNECT"和"SCANNING..."；"SCANNING..."仅在主模式并绑定从设备后出现。当蓝牙连接成功时，芯片串口会发送"CONNECT OK\r\n"；如果蓝牙连接失败，芯片串口会发送"CONNECT FAIL\r\n"；蓝牙被主机断开时，芯片串口会发送"DISCONNECT\r\n"。在主模式并绑定从设备时，会发送"SCANNING..."

*/
     TRACE("-->bsp_at_cmd_connotify_get\n");
    at_cmd_t    *at_cmd=&at_cmd_cb;
    if(at_cmd->con_notify_status){
       uint8_t tx_buf[]="CONNOTIFY:1\r\n, OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else{
       uint8_t tx_buf[]="CONNOTIFY:0\r\n, OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }



}


static void bsp_at_cmd_factory_set(uint8_t *data, uint16_t len)
{
   /*

指令描述：将芯片所有参数恢复至出厂设置
• 指令类型：写入
• 指令代码：AT+FACTORY\r\n
• 支持参数：无
• 设置/响应：OK\r\n
• 备注：
   */
   TRACE("-->bsp_at_cmd_factory_set\n");
   cm_clear(BSP_AT_CMD_CM_PAGE(0));
   cm_clear(BLE_PROFILE_CM_PAGE(0));
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   sw_reset_kick(13);
}

static void bsp_at_cmd_btat_get(uint8_t *data, uint16_t len)
{
   /*

查询蓝牙AT状态
指令描述：查询蓝牙AT功能状态
• 指令类型：读取
• 指令代码：AT+BTAT?\r\n
• 支持参数：无
• 设置/响应：BTAT:X\r\n, OK\r\n
• 备注：X值为0或1，0：蓝牙AT功能关闭（默认），1：蓝牙AT功能开启。开启后，蓝牙可向芯片发送AT指令
   */
    TRACE("-->bsp_at_cmd_btat_get\n");
    at_cmd_t    *at_cmd=&at_cmd_cb;
    if(at_cmd->btat_status){
       uint8_t tx_buf[]="BTAT:1\r\n, OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else{
        uint8_t tx_buf[]="BTAT:0\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }



}

static void bsp_at_cmd_btat_set(uint8_t *data, uint16_t len)
{
   /*

指令描述：设置蓝牙AT功能状态
• 指令类型：写入
• 指令代码：AT+BTAT=X\r\n
• 支持参数：无
• 设置/响应：OK\r\n
• 备注：X值为0或1，0：蓝牙AT功能关闭（默认），1：蓝牙AT功能开启。开启后，蓝牙可向芯片发送AT指令
*/
    TRACE("-->bsp_at_cmd_btat_set\n");
    if(len==0){
        return ;
    }
    uint8_t  btat_status=string_to_uint_decimal(data);
    at_cmd_t    *at_cmd=&at_cmd_cb;
    if(btat_status){
      at_cmd->btat_status  =ENABLE;
    }
    else{
       at_cmd->btat_status  =DISABLE;
    }

   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   at_cmd_param_save_flash();
}


static void bsp_at_cmd_ble_mtu_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_mtu_get\n");

   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+MTU:%d",128);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}

static void bsp_at_cmd_uart_pack_len_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_uart_pack_len_get\n");
   at_cmd_t    *at_cmd=&at_cmd_cb;
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+PKTLEN:%d",at_cmd->uart_pack_len);
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_uart_pack_len_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_uart_pack_len_set\n");
    if(len==0){
        return ;
    }

      at_cmd_t *at_cmd=&at_cmd_cb;
      uint16_t  pack_len =string_to_uint_decimal(data);
      if(pack_len>HSUART_TRANSFER_MAX_LEX){
         return ;
      }
     at_cmd->uart_pack_len=pack_len;
     uint8_t tx_buf[]="OK\r\n";
     bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
     extern void hsuart_transfer_init(u32 baud);
     hsuart_transfer_init(at_cmd->uart_baud);
     at_cmd_param_save_flash();
}

static void bsp_at_cmd_ble_con_status_get(uint8_t *data, uint16_t len)
{
/*查询模块连接状态
指令描述：查询模块的蓝牙连接状态
• 指令类型：读取
• 指令代码：AT+LINK?\r\n
• 支持参数：无
• 设置/响应：+LINK:XLine\r\n, OK\r\n
• 备注：X值为ON或OFF，+LINK:OnLine表示蓝牙已连接，+LINK:OffLine表示蓝牙未连接
*/
   TRACE("-->bsp_at_cmd_ble_con_status_get\n");
   at_cmd_t *at_cmd=&at_cmd_cb;
   if( at_cmd->con_handle){
      uint8_t tx_buf[]="+LINK:ON Line\r\n, OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }
   else{
      uint8_t tx_buf[]="+LINK:OFF Line\r\n, OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }

}

static void bsp_at_cmd_ble_notify_enable_status_get(uint8_t *data, uint16_t len)
{
/*查询模块连接状态
指令描述：查询模块的蓝牙连接状态
• 指令类型：读取
• 指令代码：AT+LINK?\r\n
• 支持参数：无
• 设置/响应：+LINK:XLine\r\n, OK\r\n
• 备注：X值为ON或OFF，+LINK:OnLine表示蓝牙已连接，+LINK:OffLine表示蓝牙未连接
*/
   TRACE("-->bsp_at_cmd_ble_notify_enable_status_get\n");
   at_cmd_t *at_cmd=&at_cmd_cb;
   if( at_cmd->notify_enable_status){
      uint8_t tx_buf[]="+BLENOTIFY:ON Line\r\n, OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }
   else{
        uint8_t tx_buf[]="+BLENOTIFY:OFF Line\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   }

}

static void bsp_at_cmd_ble_tx_power_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_tx_power_get\n");
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+POWER:%ddBm",xcfg_cb.rf_tx_pwr);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_tx_power_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_tx_power_set\n");

   if(len==0){
       return ;
   }
   uint8_t tx_buf[]="OK\r\n";
   at_cmd_t *at_cmd=&at_cmd_cb;
   char tx_power =string_to_int_decimal(data);
   if(tx_power==BLE_TXPWR_0DB||tx_power==BLE_TXPWR_4DB||tx_power==BLE_TXPWR_8DB){
        ble_rf_set_tx_pwr_2(tx_power,0);
        at_cmd->tx_power=tx_power;
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
         at_cmd_param_save_flash();
   }else if(tx_power==-4){
      ble_rf_set_tx_pwr_2(BLE_TXPWR_MINUS_4DB,0);
      at_cmd->tx_power=tx_power;
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       at_cmd_param_save_flash();

   }else if(tx_power==-6){
       ble_rf_set_tx_pwr_2(BLE_TXPWR_MINUS_6DB,0);
       at_cmd->tx_power=tx_power;
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        at_cmd_param_save_flash();
   }

}


static void bsp_at_cmd_device_pwr_status_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_notify_enable_status_get\n");
   uint8_t tx_buf[32]={0};
   at_cmd_t *at_cmd=&at_cmd_cb;

   uint8_t vbat_val1= at_cmd->vbat_vol/1000;
   uint8_t vbat_val2= (at_cmd->vbat_vol%1000)/100;
   sprintf((char*)tx_buf,"+PWR:%d.%dV",vbat_val1,vbat_val2);
    bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}





static void bsp_at_cmd_ble_service_uuid_get(uint8_t *data, uint16_t len)
{
     TRACE("-->bsp_at_cmd_ble_service_uuid_get\n");

    uint8_t uuid_type=0;
    const uint8_t *p_uuid = service_uuid_get(&uuid_type,2);
    uint8_t tx_buf[64]={0};
    if(p_uuid==NULL){
        printf("[ERROR] The service index value is too large\n");
        return ;
    }

    if(uuid_type==UUID_128_TYPE){
        printf("-->uuid128\r\n");
        sprintf((char*)tx_buf,"+SUUID:%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",p_uuid[15],
        p_uuid[14], p_uuid[13],p_uuid[12],p_uuid[11],p_uuid[10],p_uuid[9],p_uuid[8],p_uuid[7],p_uuid[6],
        p_uuid[5],p_uuid[4],p_uuid[3],p_uuid[2],p_uuid[1], p_uuid[0]);
    }else  if(uuid_type==UUID_16_TYPE){
       printf("-->uuid16\r\n");
       sprintf((char*)tx_buf,"+SUUID:%02X%02X", p_uuid[1], p_uuid[0]);
    }

   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));


}

static void bsp_at_cmd_ble_service_uuid_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_service_uuid_set\n");
   uint8_t uuid_hex[16]={0};
   uint16_t uuid_len=0;
   uint8_t tx_buf[]="OK\r\n";

    uint8_t uuid_type=0;
    uint8_t  *p_uuid = NULL;
    p_uuid =service_uuid_get(&uuid_type,2);


   if(len==4){
      string_to_hex((char*)data,uuid_hex,&uuid_len);
      print_r(uuid_hex,len/2);

     if(uuid_type==UUID_128_TYPE){
        p_uuid[15]=uuid_hex[0];
        p_uuid[14]=uuid_hex[1];
     }else  if(uuid_type==UUID_16_TYPE){
        p_uuid[1]=uuid_hex[0];
        p_uuid[0]=uuid_hex[1];
     }
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
      at_profile_data_save_flash();
   }else if(len==32){
      string_to_hex((char*)data,uuid_hex,&uuid_len);
      print_r(uuid_hex,len/2);

      if(uuid_type==UUID_128_TYPE){
        p_uuid[15]=uuid_hex[0];
        p_uuid[14]=uuid_hex[1];
        p_uuid[13]=uuid_hex[2];
        p_uuid[12]=uuid_hex[3];
        p_uuid[11]=uuid_hex[4];
        p_uuid[10]=uuid_hex[5];
        p_uuid[9]=uuid_hex[6];
        p_uuid[8]=uuid_hex[7];
        p_uuid[7]=uuid_hex[8];
        p_uuid[6]=uuid_hex[9];
        p_uuid[5]=uuid_hex[10];
        p_uuid[4]=uuid_hex[11];
        p_uuid[3]=uuid_hex[12];
        p_uuid[2]=uuid_hex[12];
        p_uuid[1]=uuid_hex[14];
        p_uuid[0]=uuid_hex[15];

     }else  if(uuid_type==UUID_16_TYPE){
        p_uuid[1]=uuid_hex[0];
        p_uuid[0]=uuid_hex[1];
     }
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
      at_profile_data_save_flash();
   }

}


static void bsp_at_cmd_ble_write_chara_uuid_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_write_chara_uuid_get\n");

   uint8_t *charac_uuid =charac_uuid_get(2,0);
   if(charac_uuid==NULL){
      return ;
   }
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+WUUID:%02X%02X", charac_uuid[1], charac_uuid[0]);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}
static void bsp_at_cmd_ble_read_chara_uuid_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_read_chara_uuid_get\n");
   uint8_t *charac_uuid =charac_uuid_get(2,1);
   if(charac_uuid==NULL){
      return ;
   }
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+RUUID:%02X%02X", charac_uuid[1], charac_uuid[0]);
    bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_write_chara_uuid_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_write_chara_uuid_get\n");
   uint8_t charac_uuid_val[4]={0};
   uint16_t uuid_size=0;
   if(len==4){
      string_to_hex((char*)data,charac_uuid_val,&uuid_size);
      uint8_t *charac_uuid =charac_uuid_get(2,0);
      if(charac_uuid==NULL){
          return ;
      }
      charac_uuid[0]=charac_uuid_val[1];
      charac_uuid[1]=charac_uuid_val[0];
      uint8_t tx_buf[]="OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       at_profile_data_save_flash();
   }
   else{
       return ;
   }
}
static void bsp_at_cmd_ble_read_chara_uuid_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_read_chara_uuid_set\n");
   uint8_t charac_uuid_val[4]={0};
   uint16_t uuid_size=0;
   if(len==4){
      string_to_hex((char*)data,charac_uuid_val,&uuid_size);
      uint8_t *charac_uuid =charac_uuid_get(2,1);
      if(charac_uuid==NULL){
          return ;
      }
      charac_uuid[0]=charac_uuid_val[1];
      charac_uuid[1]=charac_uuid_val[0];
      uint8_t tx_buf[]="OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       at_profile_data_save_flash();
   }
   else{
       return ;
   }
}


static void bsp_at_cmd_ble_ota_write_chara_uuid_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_ota_write_chara_uuid_get\n");

   uint8_t *charac_uuid =charac_uuid_get(0,0);
   if(charac_uuid==NULL){
      return ;
   }
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+OTAWUUID:%02X%02X", charac_uuid[1], charac_uuid[0]);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}
static void bsp_at_cmd_ble_ota_read_chara_uuid_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_ota_read_chara_uuid_get\n");

   uint8_t *charac_uuid =charac_uuid_get(0,0);
   if(charac_uuid==NULL){
      return ;
   }
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+OTARUUID:%02X%02X", charac_uuid[1], charac_uuid[0]);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}
static void bsp_at_cmd_ble_ota_write_chara_uuid_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_ota_write_chara_uuid_set\n");
   uint8_t charac_uuid_val[4]={0};
   uint16_t uuid_size=0;
   if(len==4){
      string_to_hex((char*)data,charac_uuid_val,&uuid_size);
      uint8_t *charac_uuid =charac_uuid_get(0,0);
      if(charac_uuid==NULL){
          return ;
      }
      charac_uuid[0]=charac_uuid_val[1];
      charac_uuid[1]=charac_uuid_val[0];
      uint8_t tx_buf[]="OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       at_profile_data_save_flash();
   }
   else{
       return ;
   }
}

static void bsp_at_cmd_ble_ota_read_chara_uuid_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_ota_read_chara_uuid_set\n");
   uint8_t charac_uuid_val[4]={0};
   uint16_t uuid_size=0;
   if(len==4){
      string_to_hex((char*)data,charac_uuid_val,&uuid_size);
      uint8_t *charac_uuid =charac_uuid_get(0,1);
      if(charac_uuid==NULL){
         return ;
      }
      charac_uuid[0]=charac_uuid_val[1];
      charac_uuid[1]=charac_uuid_val[0];
      uint8_t tx_buf[]="OK\r\n";
      bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
      at_profile_data_save_flash();
   }
   else{
       return ;
   }
}

static void bsp_at_cmd_ble_mac_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_mac_get\n");
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"+MAX:%02X%02X%02X%02X%02X%02X",xcfg_cb.le_addr[0],xcfg_cb.le_addr[1],
           xcfg_cb.le_addr[2],xcfg_cb.le_addr[3],xcfg_cb.le_addr[4],xcfg_cb.le_addr[5]);

   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_mac_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_mac_set\n");

   if(len==12) {
	   uint8_t addr_buf[6]={0};
       uint16_t addr_len=0;
	   at_cmd_t *at_cmd=&at_cmd_cb;
	   string_to_hex((char*)data,addr_buf,&addr_len);
       memcpy(xcfg_cb.le_addr,addr_buf,6);
	   memcpy(at_cmd->ble_addr,xcfg_cb.le_addr,6);
	   bsp_ble_init();
	   uint8_t tx_buf[]="OK\r\n";
	   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
	   at_cmd_param_save_flash();
   }


}


static void bsp_at_cmd_ble_adv_interval_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_adv_interval_get\n");

   uint8_t tx_buf[32]={0};

   uint16_t interval=0;
   ble_get_adv_interval(&interval);
   //interval=interval*10/16;
   sprintf((char*)tx_buf,"+ADVINT:%d",interval);
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}

static void bsp_at_cmd_ble_adv_interval_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_adv_interval_set\n");

   if(len==0){
      return;
   }

   uint16_t  adv_interval =string_to_uint_decimal(data);

   if(adv_interval<20||adv_interval>200){
      return ;
   }
   at_cmd_t *at_cmd=&at_cmd_cb;
   //adv_interval=adv_interval*16/10;
   at_cmd->adv_interval=adv_interval;
   ble_set_adv_interval(adv_interval);
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   at_cmd_param_save_flash();
}

static void bsp_at_cmd_ble_adv_status_get(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_adv_status_get\n");
    if(ble_adv_is_en()==true){
        uint8_t tx_buf[]="+ADV:1\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else{
       uint8_t tx_buf[]="+ADV:0\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}

static void bsp_at_cmd_ble_adv_status_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_adv_status_set\n");
    if(len==0){
      return;
   }

   uint16_t  adv_status =string_to_uint_decimal(data);
   if(adv_status){
      ble_adv_en();
   }
   else{
      ble_adv_dis();
   }
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   at_cmd_param_save_flash();
}


static void bsp_at_cmd_ble_adv_data_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_adv_data_get\n");

   at_cmd_t *at_cmd=&at_cmd_cb;
   if(at_cmd->scan_rsp_len==0){
      uint8_t tx_buf[32]="+RESE: \r\n, OK\r\n";
      bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
      return ;
   }

   uint8_t tx_buf[64]="+RESE:";
   uint8_t tx_len=strlen((char*)tx_buf);
   for(uint8_t i=0;i<at_cmd->scan_rsp_len;i++){
        sprintf((char*)&tx_buf[tx_len],"%02X",at_cmd->scan_rsp_buf[i]);
        tx_len=tx_len+2;
   }
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
    uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_adv_data_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_adv_data_set\n");
   if(len==0 || len>31){
      return;
   }

   at_cmd_t *at_cmd=&at_cmd_cb;
   string_to_hex((char*)data,at_cmd->scan_rsp_buf,&at_cmd->scan_rsp_len);
   TRACE("at_cmd->scan_rsp_len = %d \n",at_cmd->scan_rsp_len);
   print_r(at_cmd->scan_rsp_buf,at_cmd->scan_rsp_len);

   ble_adv_dis();
   uint8_t scan_rsp_temp[31] = {0};
   uint32_t rsp_len = ble_get_scan_data(scan_rsp_temp, sizeof(scan_rsp_temp));
   bool ret = ble_set_scan_rsp_data(scan_rsp_temp, rsp_len);
    if(ret == false)
    {
        ble_adv_en();
        uint8_t tx_buf[]="set scan rsp fail!!!\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return ;
    }
    ble_adv_en();
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    at_cmd_param_save_flash();
}


static void bsp_at_cmd_ble_adv_data_off(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_adv_data_off\n");

    if(len==0){
      return;
    }
    uint8_t adv_data_status= string_to_uint_decimal(data);
    at_cmd_t *at_cmd=&at_cmd_cb;
    if(adv_data_status){
        at_cmd->scan_rsp_status=ENABLE;
    }
    else{
         at_cmd->scan_rsp_status=DISABLE;
    }

   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

static void bsp_at_cmd_ble_con_password_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_con_password_get\n");
   uint8_t tx_buf[32]={0};
   at_cmd_t *at_cmd=&at_cmd_cb;
   sprintf((char*)tx_buf,"+PASSWORD:%d",(int)at_cmd->password);
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_con_password_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_con_password_set\n");
   if(len==6){
       at_cmd_t *at_cmd=&at_cmd_cb;
       for(uint8_t i=0;i<6;i++){
           if(data[i]<'0'||data[i]>'9'){
                return ;
           }
       }
       at_cmd->password=  string_to_uint_decimal(data);
       uint8_t tx_buf[]="OK\r\n";
       bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       at_cmd_param_save_flash();
   }

}

static void bsp_at_cmd_ble_con_password_clear(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_con_password_clear\n");
   at_cmd_t *at_cmd=&at_cmd_cb;
   at_cmd->password=0;
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
   at_cmd_param_save_flash();
}

static void bsp_at_cmd_ble_update_con_param_set(uint8_t *data, uint16_t len)
{
    /*
    设置模块连接间隔参数
指令描述：设置模块连接间隔参数
• 指令类型：写入
• 指令代码：AT+CONPARAM=X\r\n
• 支持参数：无
• 设置/响应：OK\r\n
• 备注：
"AT+CONPARAM=0\r\n"：最小连接间隔[6]，最大连接间隔[12]，从机延迟(0)，连接超时[300]
"AT+CONPARAM=1\r\n"：最小连接间隔[160]，最大连接间隔[180]，从机延迟(4)，连接超时[600]

    */

   TRACE("-->bsp_at_cmd_ble_update_con_param_set\n");
   if(len==0){
       return ;
   }

    uint8_t  con_param_flag=  string_to_uint_decimal(data);
     at_cmd_t *at_cmd=&at_cmd_cb;
    if(con_param_flag)
    {

        at_cmd->min_con_interval=6;
        at_cmd->max_con_interval=12;
        at_cmd->slavery_latency=0;
        at_cmd->con_timeout=300;
    }
    else {
        at_cmd->min_con_interval=160;
        at_cmd->max_con_interval=180;
        at_cmd->slavery_latency=4;
        at_cmd->con_timeout=600;
    }
    uint8_t tx_buf[]="OK\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));

 #if 0
  if(at_cmd->con_handle){
       ble_update_conn_param_2(at_cmd->con_handle,
                               at_cmd->min_con_interval,
                               at_cmd->max_con_interval,
                               at_cmd->slavery_latency,
                               at_cmd->con_timeout);
        for(uint8_t i=0;i<100;i++){
            delay_5ms(1);
            if(at_cmd->update_con_param_done){
               uint8_t tx_buf[]="OK\r\n";
               at_cmd->update_con_param_done=0;
               bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
               break;
            }
        }
  }
  else{
     return ;
  }
#endif // 0
}

static void bsp_at_cmd_ble_min_con_interval_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_min_con_interval_get\n");
   uint8_t tx_buf[32]={0};
   at_cmd_t *at_cmd=&at_cmd_cb;
   sprintf((char*)tx_buf,"+CONINTMIN:%d",at_cmd->min_con_interval);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_min_con_interval_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_min_con_interval_set\n");
   if(len==0){
       return ;
   }
   at_cmd_t *at_cmd=&at_cmd_cb;
   at_cmd->min_con_interval =string_to_uint_decimal(data);
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));

}

static void bsp_at_cmd_ble_max_con_interval_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_max_con_interval_get\n");

   uint8_t tx_buf[32]={0};
   at_cmd_t *at_cmd=&at_cmd_cb;
   sprintf((char*)tx_buf,"+CONINTMAX:%d",at_cmd->max_con_interval);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_max_con_interval_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_max_con_interval_set\n");

   if(len==0){
       return ;
   }
   at_cmd_t *at_cmd=&at_cmd_cb;
   at_cmd->max_con_interval =string_to_uint_decimal(data);
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

static void bsp_at_cmd_ble_slavery_latency_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_slavery_latency_get\n");

   uint8_t tx_buf[32]={0};
   at_cmd_t *at_cmd=&at_cmd_cb;
   sprintf((char*)tx_buf,"+LATENCY:%d",at_cmd->slavery_latency);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_slavery_latency_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_slavery_latency_set\n");

   if(len==0){
       return ;
   }
   at_cmd_t *at_cmd=&at_cmd_cb;
   at_cmd->slavery_latency =string_to_uint_decimal(data);
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

static void bsp_at_cmd_ble_con_timeout_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_con_timeout_get\n");

   uint8_t tx_buf[32]={0};
   at_cmd_t *at_cmd=&at_cmd_cb;
   sprintf((char*)tx_buf,"+CONTIMEOUT:%d",at_cmd->con_timeout);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_ble_con_timeout_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_con_timeout_set\n");

   if(len==0){
       return ;
   }
   at_cmd_t *at_cmd=&at_cmd_cb;
   at_cmd->con_timeout =string_to_uint_decimal(data);
   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));

}

static void bsp_at_cmd_ble_stack_version_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_stack_version_get\n");
   uint8_t tx_buf[]="+BLESTACK?5.4\r\n, OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

static void bsp_at_cmd_encryption_status_set(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_encryption_status_set\n");

   if(len==0){
       return ;
   }

   uint8_t enc_status=string_to_uint_decimal(data);
   at_cmd_t *at_cmd=&at_cmd_cb;
   if(enc_status){
       at_cmd->enc_status=ENABLE;
   }
   else{
      at_cmd->enc_status=DISABLE;
   }

   uint8_t tx_buf[]="OK\r\n";
   bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

static void bsp_at_cmd_ble_rssi_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_ble_rssi_get\n");
   at_cmd_t    *at_cmd=&at_cmd_cb;
   if(at_cmd->con_handle) {
        at_cmd->rssi_val=  ble_get_conn_rssi(at_cmd->con_handle);
    }
     else{
       return ;
   }

   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"RSSI=%d dBm",at_cmd->rssi_val);
    bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));

}
static void bsp_at_cmd_device_vdd_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_device_vdd_get\n");

    at_cmd_t    *at_cmd=&at_cmd_cb;
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"VDD=%d mV",at_cmd->device_vdd);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
    uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

static void bsp_at_cmd_device_temp_get(uint8_t *data, uint16_t len)
{
   TRACE("-->bsp_at_cmd_device_temp_get\n");
   at_cmd_t    *at_cmd=&at_cmd_cb;
   uint8_t tx_buf[32]={0};
   sprintf((char*)tx_buf,"TEMP=%d °C",at_cmd->device_temp);
   bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
   uint8_t ack_buf[]="\r\n, OK\r\n";
   bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
}

// 新增：BLE初始化（修正版）
static void bsp_at_cmd_ble_init_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_init_set\n");
    
    if(len == 0)
    {
        // 默认使用当前配置重新初始化
        bsp_ble_init();
        uint8_t tx_buf[]="BLE REINIT OK\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint8_t role = string_to_uint_decimal(data);
    
    // 验证角色参数（虽然不影响实际功能，但提供反馈）
    switch(role)
    {
        case 0: // Peripheral only
        case 1: // Central only  
        case 2: // Both (default)
            // 当前SDK配置已经是双角色模式，无需更改
            // 只是重新初始化BLE栈
            bsp_ble_init();
            break;
        default:
        {
            uint8_t tx_buf[]="ERROR: Invalid role (0=Peripheral, 1=Central, 2=Both)\r\n";
            bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
            return;
        }
    }
    
    uint8_t tx_buf[]="BLE INIT OK\r\n, OK\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

// 新增：BLE反初始化
static void bsp_at_cmd_ble_deinit_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_deinit_set\n");
    
    // 停止广播和扫描
    ble_adv_dis();
    ble_scan_dis();
    
    // 断开所有连接
    ble_cb_t *ble_cb_node = ble_get_ble_info();
    if(ble_cb_node != NULL)
    {
        for(uint8_t i = 0; i < MAX_BLE_CONNECTIONS; i++)
        {
            if(ble_cb_node[i].conhdl != 0)
            {
                ble_disconnect(ble_cb_node[i].conhdl);
            }
        }
    }
    
    // 清空连接信息
    memset(&ble_cb, 0, sizeof(ble_cb));
    for(uint8_t i = 0; i < MAX_BLE_CONNECTIONS; i++)
    {
        ble_cb[i].role = ROLE_UNKNOWN;
    }
    
    at_cmd_cb.con_handle = 0;
    
    uint8_t tx_buf[]="BLE DEINIT OK\r\n, OK\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

// 新增：查询连接状态
static void bsp_at_cmd_ble_conn_status_get(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_conn_status_get\n");
    
    ble_cb_t *ble_cb_node = ble_get_ble_info();
    if(ble_cb_node == NULL)
    {
        uint8_t tx_buf[]="+BLECONN:0\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint8_t conn_count = 0;
    uint16_t conn_handles[MAX_BLE_CONNECTIONS] = {0};
    
    for(uint8_t i = 0; i < MAX_BLE_CONNECTIONS; i++)
    {
        if(ble_cb_node[i].conhdl != 0)
        {
            conn_handles[conn_count] = ble_cb_node[i].conhdl;
            conn_count++;
        }
    }
    
    uint8_t tx_buf[128] = {0};
    if(conn_count == 0)
    {
        sprintf((char*)tx_buf, "+BLECONN:0\r\n, OK\r\n");
    }
    else
    {
        sprintf((char*)tx_buf, "+BLECONN:%d,", conn_count);
        uint8_t pos = strlen((char*)tx_buf);
        for(uint8_t i = 0; i < conn_count; i++)
        {
            sprintf((char*)&tx_buf[pos], "0x%04X", conn_handles[i]);
            pos = strlen((char*)tx_buf);
            if(i < conn_count - 1)
            {
                tx_buf[pos++] = ',';
            }
        }
        strcat((char*)tx_buf, "\r\n, OK\r\n");
    }
    
    bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
}

// 新增：发送Indicate
static void bsp_at_cmd_ble_indicate_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_indicate_set\n");
    
    if(data == NULL || len == 0 || len > 244)
    {
        uint8_t tx_buf[]="ERROR: Invalid data\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    ble_cb_t *ble_cb_node = ble_get_ble_info();
    if(ble_cb_node == NULL)
    {
        uint8_t tx_buf[]="ERROR: No BLE info\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint8_t target_conidx = INVALID_CONIDX;
    for(uint8_t i = 0; i < MAX_BLE_CONNECTIONS; i++)
    {
        if(ble_cb_node[i].conhdl != 0 && 
           (ble_cb_node[i].role == ROLE_MASTER || ble_cb_node[i].role == ROLE_SLAVE))
        {
            target_conidx = i;
            break;
        }
    }
    
    if(target_conidx == INVALID_CONIDX)
    {
        uint8_t tx_buf[]="ERROR: No active connection\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    ATT_ERR_CODE ret = ble_indicate_for_handle(ble_cb_node[target_conidx].conhdl, 
                                              ATT_CHARACTERISTIC_FF22_01_VALUE_HANDLE, 
                                              data, len);
    
    if(ret == ATT_ERR_NO_ERR)
    {
        uint8_t tx_buf[]="INDICATE OK\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else
    {
        uint8_t tx_buf[]="ERROR: Indicate failed\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}

// 新增：发现特征值
static void bsp_at_cmd_ble_discover_char_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_discover_char_set\n");
    
    if(len == 0)
    {
        uint8_t tx_buf[]="ERROR: Invalid service handle\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    at_cmd_t *at_cmd = &at_cmd_cb;
    if(at_cmd->con_handle == 0)
    {
        uint8_t tx_buf[]="ERROR: Not connected\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint16_t service_handle = string_to_uint_decimal(data);
    
    gatt_client_service_t service;
    memset(&service, 0, sizeof(gatt_client_service_t));
    service.start_group_handle = service_handle;
    service.end_group_handle = 0xFFFF;  // 使用标准的最大句柄值
    
    uint8_t ret = ble_client_discover_characteristics_for_service(at_cmd->con_handle, &service);
    
    if(ret == 0)
    {
        uint8_t tx_buf[]="DISCOVER CHAR STARTED\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else
    {
        uint8_t tx_buf[]="ERROR: Discover char failed\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}
/**
 * @brief 在已发现的服务器信息中，根据连接句柄和特征值句柄查找完整的特征结构体
 * @param conn_handle 连接句柄
 * @param value_handle 要查找的特征值句柄
 * @return 找到的特征指针，失败返回 NULL
 */
static gatt_client_characteristic_t* find_characteristic_by_value_handle(uint16_t conn_handle, uint16_t value_handle)
{
    uint8_t conidx = ble_get_conidx(conn_handle);
    if (conidx == INVALID_CONIDX) {
        return NULL;
    }

    server_info_t *p_svc_info = ble_get_server_info(conidx);
    if (p_svc_info->characteristic_cnt == 0) {
        return NULL;
    }

    for (uint8_t i = 0; i < p_svc_info->characteristic_cnt; i++) {
        gatt_client_characteristic_t *ch = &p_svc_info->characteristic[i];
        // 注意：有些 SDK 的特征值句柄字段可能是 value_handle，请根据实际定义调整
        if (ch->value_handle == value_handle) {
            return ch;
        }
    }
    return NULL;
}

/**
 * @brief AT+WRITE 指令处理：向对端设备写入特征值
 * 格式：AT+WRITE=handle,hex_data[,response]
 *  - handle: 特征值句柄（十六进制或十进制）
 *  - hex_data: 要写入的数据（十六进制字符串）
 *  - response: 可选，0=无响应写入(Write Command)，1=需要响应(Write Request)，默认=1
 * 示例：
 *   AT+WRITE=002A,01020304         # 向句柄 0x2A 写入 0x01 0x02 0x03 0x04（带响应）
 *   AT+WRITE=002A,01020304,0       # 同上，但无响应写入
 */
static void bsp_at_cmd_ble_write_characteristic(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_write_characteristic\n");

    at_cmd_t *at_cmd = &at_cmd_cb;
    if (at_cmd->con_handle == 0) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Not connected\r\n", 21);
        return;
    }

    // ---------- 1. 查找第一个逗号（分隔句柄和数据） ----------
    int comma1_pos = -1;
    for (int i = 0; i < len; i++) {
        if (data[i] == ',') {
            comma1_pos = i;
            break;
        }
    }
    if (comma1_pos == -1) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Missing comma\r\n", 22);
        return;
    }

    // ---------- 2. 解析特征值句柄 ----------
    uint8_t handle_str[10] = {0};
    uint8_t handle_len = (comma1_pos < (int)sizeof(handle_str)-1) ? comma1_pos : sizeof(handle_str)-1;
    memcpy(handle_str, data, handle_len);
    uint16_t char_value_handle = (uint16_t)string_to_uint_decimal(handle_str); // 支持十进制或十六进制（需完善string_to_uint_decimal支持0x前缀，若目前不支持可先要求十进制）
    
    if (char_value_handle == 0) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Invalid handle\r\n", 23);
        return;
    }

    // ---------- 3. 查找第二个逗号（分隔数据与 response 标志） ----------
    int comma2_pos = -1;
    for (int i = comma1_pos + 1; i < len; i++) {
        if (data[i] == ',') {
            comma2_pos = i;
            break;
        }
    }

    uint8_t with_response = 1;  // 默认带响应（Write Request）
    uint8_t *hex_data_start = &data[comma1_pos + 1];
    uint16_t hex_data_len = 0;

    if (comma2_pos != -1) {
        // 有第二个逗号：解析 response 标志
        hex_data_len = comma2_pos - (comma1_pos + 1);
        uint8_t resp_str[5] = {0};
        uint8_t resp_len = len - (comma2_pos + 1);
        if (resp_len < sizeof(resp_str)) {
            memcpy(resp_str, &data[comma2_pos + 1], resp_len);
            with_response = (uint8_t)string_to_uint_decimal(resp_str);
        }
    } else {
        // 没有第二个逗号：数据部分到结尾
        hex_data_len = len - (comma1_pos + 1);
    }

    if (hex_data_len == 0) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: No data provided\r\n", 25);
        return;
    }

    // ---------- 4. 十六进制数据转二进制 ----------
    uint8_t write_data[64] = {0};
    uint16_t write_len = 0;
    char hex_str[128] = {0};
    if (hex_data_len >= sizeof(hex_str)) hex_data_len = sizeof(hex_str) - 1;
    memcpy(hex_str, hex_data_start, hex_data_len);
    hex_str[hex_data_len] = '\0';
    write_len = string_to_hex(hex_str, write_data, NULL);
    
    if (write_len == 0) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Invalid hex data\r\n", 25);
        return;
    }

    // ---------- 5. 查找已发现的完整特征结构体 ----------
    gatt_client_characteristic_t *found_char = find_characteristic_by_value_handle(at_cmd->con_handle, char_value_handle);
    if (found_char == NULL) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Characteristic not discovered\r\n", 36);
        return;
    }

    // ---------- 6. 执行写操作 ----------
    ATT_ERR_CODE ret;
    if (with_response) {
        ret = ble_write_req_for_character(at_cmd->con_handle, found_char, write_data, write_len);
    } else {
        ret = ble_write_cmd_for_character(at_cmd->con_handle, found_char, write_data, write_len);
    }

    if (ret == ATT_ERR_NO_ERR) {
        bsp_at_cmd_send_data((uint8_t*)"WRITE OK\r\n", 10);
    } else {
        TRACE("write_char = %d\n", ret);
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Write failed\r\n", 21);
    }
}

/**
 * @brief AT+CCCD 指令处理：配置客户端特征配置描述符（使能通知/指示）
 * 格式：AT+CCCD=handle,notify,indicate
 *  - handle: 特征值句柄（十六进制或十进制）
 *  - notify:  0=禁用通知，1=使能通知
 *  - indicate:0=禁用指示，1=使能指示
 * 示例：
 *   AT+CCCD=002A,1,0   # 使能句柄 0x2A 的特征通知
 */
static void bsp_at_cmd_ble_cccd_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_cccd_set\n");

    if (len < 5) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Format CCCD=handle,notify,indicate\r\n", 43);
        return;
    }

    at_cmd_t *at_cmd = &at_cmd_cb;
    if (at_cmd->con_handle == 0) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Not connected\r\n", 21);
        return;
    }

    // ---------- 1. 定位两个逗号 ----------
    int comma1_pos = -1, comma2_pos = -1;
    for (int i = 0; i < len; i++) {
        if (data[i] == ',') {
            if (comma1_pos == -1) comma1_pos = i;
            else { comma2_pos = i; break; }
        }
    }
    if (comma1_pos == -1 || comma2_pos == -1) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Format CCCD=handle,notify,indicate\r\n", 43);
        return;
    }

    // ---------- 2. 解析句柄 ----------
    uint8_t handle_str[10] = {0};
    uint8_t handle_len = (comma1_pos < (int)sizeof(handle_str)-1) ? comma1_pos : sizeof(handle_str)-1;
    memcpy(handle_str, data, handle_len);
    uint16_t char_value_handle = (uint16_t)string_to_uint_decimal(handle_str);
    if (char_value_handle == 0) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Invalid handle\r\n", 23);
        return;
    }

    // ---------- 3. 解析通知/指示标志 ----------
    uint8_t notify_str[5] = {0}, indicate_str[5] = {0};
    uint8_t notify_len = comma2_pos - comma1_pos - 1;
    uint8_t indicate_len = len - comma2_pos - 1;
    if (notify_len >= sizeof(notify_str) || indicate_len >= sizeof(indicate_str)) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Parameter too long\r\n", 26);
        return;
    }
    memcpy(notify_str, &data[comma1_pos + 1], notify_len);
    memcpy(indicate_str, &data[comma2_pos + 1], indicate_len);
    uint8_t enable_notify = (uint8_t)string_to_uint_decimal(notify_str);
    uint8_t enable_indicate = (uint8_t)string_to_uint_decimal(indicate_str);

    // ---------- 4. 计算配置值 ----------
    uint16_t config = 0;
    if (enable_notify)  config |= GATT_CCC_NOTIFICATION;
    if (enable_indicate) config |= GATT_CCC_INDICATION;
    if (config == 0) {
        // 允许写入 0 以禁用所有通知/指示
    }

    // ---------- 5. 查找完整的特征结构体 ----------
    gatt_client_characteristic_t *found_char = find_characteristic_by_value_handle(at_cmd->con_handle, char_value_handle);
    if (found_char == NULL) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Characteristic not discovered\r\n", 36);
        return;
    }

    // ---------- 6. 执行 CCCD 写入 ----------
    uint8_t ret = ble_client_write_client_characteristic_configuration(at_cmd->con_handle, found_char, config);
    if (ret == ATT_ERR_NO_ERR) {
        bsp_at_cmd_send_data((uint8_t*)"CCCD OK\r\n", 9);
    } else {
        TRACE("CCCD write = %d\n", ret);
        bsp_at_cmd_send_data((uint8_t*)"ERROR: CCCD write failed\r\n", 26);
    }
}

// 新增：查询绑定设备列表
static void bsp_at_cmd_ble_bond_list_get(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_bond_list_get\n");
    
    uint8_t bonding_cnt = ble_get_bonding_cnt();
    
    if(bonding_cnt == 0)
    {
        uint8_t tx_buf[]="+BONDLIST:0\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint8_t tx_buf[256] = {0};
    sprintf((char*)tx_buf, "+BONDLIST:%d", bonding_cnt);
    uint8_t pos = strlen((char*)tx_buf);
    
    for(uint8_t i = 0; i < bonding_cnt && i < 10; i++) // 限制最多显示10个
    {
        sm_bonding_info_t* info = ble_get_bonding_info_for_index(i);
        if(info)
        {
            sprintf((char*)&tx_buf[pos], ",%02X%02X%02X%02X%02X%02X", 
                    info->address[0], info->address[1], info->address[2],
                    info->address[3], info->address[4], info->address[5]);
            pos = strlen((char*)tx_buf);
        }
    }
    
    strcat((char*)tx_buf, "\r\n, OK\r\n");
    bsp_at_cmd_send_data(tx_buf, strlen((char*)tx_buf));
}

// 新增：删除绑定设备
static void bsp_at_cmd_ble_bond_del_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_bond_del_set\n");
    
    if(len < 12)
    {
        uint8_t tx_buf[]="ERROR: Invalid MAC address\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint8_t mac_addr[6] = {0};
    uint16_t addr_len = 0;
    string_to_hex((char*)data, mac_addr, &addr_len);
    
    if(addr_len != 6)
    {
        uint8_t tx_buf[]="ERROR: Invalid MAC format\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    bool ret = ble_delete_bonding_for_addr(GAP_PUBLIC_ADDRESS_TYPE, *(bd_addr_t*)mac_addr);
    
    if(ret)
    {
        uint8_t tx_buf[]="BOND DELETE OK\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else
    {
        uint8_t tx_buf[]="ERROR: Bond delete failed\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}

// 新增：清除所有绑定
static void bsp_at_cmd_ble_bond_clear_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_bond_clear_set\n");
    
    ble_delete_bonding_all();
    
    uint8_t tx_buf[]="ALL BONDS CLEARED\r\n, OK\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

static void bsp_at_cmd_ble_scan_status_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_scan_status_set\n");
    
    if(len == 0)
    {
        return;
    }
    
    uint8_t scan_status = string_to_uint_decimal(data);
    
    if(scan_status)
    {
        ble_scan_en();
        uint8_t tx_buf[]="SCAN START\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else
    {
        ble_scan_dis();
        uint8_t tx_buf[]="SCAN STOP\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}

static void bsp_at_cmd_ble_connect_by_addr(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_connect_by_addr\n");
    
    if(len < 12)
    {
        uint8_t tx_buf[]="ERROR: Invalid MAC length\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint8_t mac_addr[6] = {0};
    uint16_t addr_len = 0;
    
    string_to_hex((char*)data, mac_addr, &addr_len);
    
    if(addr_len != 6)
    {
        uint8_t tx_buf[]="ERROR: Invalid MAC format\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    printf("Connecting to MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
           mac_addr[0], mac_addr[1], mac_addr[2], 
           mac_addr[3], mac_addr[4], mac_addr[5]);
    
    //处理大小端mac地址
    uint8_t mac_addr_tmp[6] = {mac_addr[5], mac_addr[4], mac_addr[3], mac_addr[2], mac_addr[1], mac_addr[0]};
    le_conn_initiate_err_code_t ret = ble_connect_by_addr(mac_addr_tmp, 0, 24, 0, 100);
    
    if(ret == LE_CONN_INITIATE_ERR_NO_ERR)
    {
        uint8_t tx_buf[]="CONNECTING...\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else
    {
        uint8_t tx_buf[]="ERROR: initiate a connection failed\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}


static void bsp_at_cmd_ble_discover_services(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_discover_services\n");
    
    at_cmd_t *at_cmd = &at_cmd_cb;
    
    if(at_cmd->con_handle == 0)
    {
        uint8_t tx_buf[]="ERROR: Not connected\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint8_t ret = ble_client_discover_primary_services(at_cmd->con_handle);
    
    if(ret == 0)
    {
        uint8_t tx_buf[]="DISCOVERING...\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else
    {
        uint8_t tx_buf[]="ERROR: Discover failed\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}

static void bsp_at_cmd_ble_read_characteristic(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_read_characteristic\n");
    
    at_cmd_t *at_cmd = &at_cmd_cb;
    
    if(at_cmd->con_handle == 0)
    {
        uint8_t tx_buf[]="ERROR: Not connected\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    uint16_t char_handle = string_to_uint_decimal(data);

    // ---------- 查找已发现的完整特征结构体 ----------
    gatt_client_characteristic_t *found_char = find_characteristic_by_value_handle(at_cmd->con_handle, char_handle);
    if (found_char == NULL) {
        bsp_at_cmd_send_data((uint8_t*)"ERROR: Characteristic not discovered\r\n", 36);
        return;
    }
    
    ATT_ERR_CODE ret = ble_read_req_for_character(at_cmd->con_handle, found_char);
    
    if(ret == ATT_ERR_NO_ERR)
    {
        uint8_t tx_buf[]="READING...\r\n, OK\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
    else
    {
        TRACE("read_char = %d\n", ret);
        uint8_t tx_buf[]="ERROR: Read failed\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
    }
}

static void bsp_at_cmd_ble_pairing_start(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_pairing_start\n");
    
    at_cmd_t *at_cmd = &at_cmd_cb;
    
    if(at_cmd->con_handle == 0)
    {
        uint8_t tx_buf[]="ERROR: Not connected\r\n";
        bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        return;
    }
    
    ble_sm_send_security_request(at_cmd->con_handle);
    
    uint8_t tx_buf[]="PAIRING STARTED\r\n, OK\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

// 新增：明确的通知指令
static void bsp_at_cmd_ble_notify_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_ble_notify_set\n");
    
    // 直接复用现有的 ble_user_notify_proc 函数
    extern bool ble_user_notify_proc(uint8_t *data, uint16_t len);
    uint8_t res_hex[256]={0};
    uint16_t hex_len=0;
    string_to_hex((char*)data,res_hex,&hex_len);
    if(ble_user_notify_proc(res_hex, hex_len))
    {
        uint8_t ack_buf[]="NOTIFY OK\r\n, OK\r\n";
        bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
    }
    else
    {
        uint8_t ack_buf[]="ERROR: Notify failed\r\n";
        bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
    }
}

static void bsp_at_cmd_error(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_error\n");
    uint8_t tx_buf[]="AT CMD ERROR\r\n";
    bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
}

static void bsp_at_cmd_data_set(uint8_t *data, uint16_t len)
{
    TRACE("-->bsp_at_cmd_data_set\n");
    extern  bool  ble_user_notify_proc(uint8_t *data,uint16_t len);
    uint8_t res_hex[256]={0};
    uint16_t hex_len=0;
    string_to_hex((char*)data,res_hex,&hex_len);
   if( ble_user_notify_proc(res_hex, hex_len))
   {
      uint8_t ack_buf[]="\r\n, OK\r\n";
        bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
   }else
   {
         uint8_t ack_buf[]="\r\n, ERROR\r\n";
        bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
   }


}

static void bsp_at_cmd_data_get(uint8_t *data, uint16_t len)
{
     TRACE("-->bsp_at_cmd_data_get\n");
     ble_data_t *ble_data= &ble_data_cb;
     if(ble_data->ble_rx_len>0){
         bsp_at_cmd_send_data(ble_data->ble_rx_data, ble_data->ble_rx_len);
         memset(ble_data,0,sizeof(ble_data_t));
     }
     else{
        //uint8_t ack_buf[]="\r\n, ERROR\r\n";
        uint8_t ack_buf[1]={0};
        bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
     }
}

extern void bsp_at_cmd_help(uint8_t *data, uint16_t len);

const bsp_at_cmd_t bsp_at_cmd[] = {
    {"AT+DATA=",	bsp_at_cmd_data_set	},
    {"AT+DATA?",	bsp_at_cmd_data_get	},
	{"AT+NAME?",	bsp_at_cmd_name_get	},
	{"AT+NAME=",	bsp_at_cmd_name_set	},
	{"AT+DISC",	    bsp_at_cmd_ble_disc	},
	{"AT+UART?",	bsp_at_cmd_baud_get	},
	{"AT+UART=",	bsp_at_cmd_baud_set	},
	{"AT",	        bsp_at_cmd_test	},
	{"AT+VER?",	    bsp_at_cmd_soft_version_get	},
	{"AT+RST",	    bsp_at_cmd_soft_reset	},
	{"AT+ECHO?",	bsp_at_cmd_echo_get},
	{"AT+ECHO=",	bsp_at_cmd_echo_set	},
	{"AT+ROLE?",	bsp_at_cmd_role_check},
	{"AT+MODE?",	bsp_at_cmd_mode_check},
	{"AT+SLEEP?",	bsp_at_cmd_sleep_check},
	{"AT+WDG?",	    bsp_at_cmd_wdt_status_get},
	{"AT+CONNOTIFY?",bsp_at_cmd_connotify_get},
	{"AT+FACTORY",   bsp_at_cmd_factory_set},
	{"AT+BTAT?",bsp_at_cmd_btat_get},
	{"AT+BTAT=",bsp_at_cmd_btat_set},
	{"AT+MTU?",bsp_at_cmd_ble_mtu_get},
	{"AT+PKTLEN?",bsp_at_cmd_uart_pack_len_get},
	{"AT+PKTLEN=",bsp_at_cmd_uart_pack_len_set},
	{"AT+LINK?",bsp_at_cmd_ble_con_status_get},
	{"AT+BLENOTIFY?",bsp_at_cmd_ble_notify_enable_status_get},
	{"AT+POWER?",bsp_at_cmd_ble_tx_power_get},
	{"AT+PWR?",bsp_at_cmd_device_pwr_status_get},
	{"AT+TPWR=",bsp_at_cmd_ble_tx_power_set},
	{"AT+SUUID?",bsp_at_cmd_ble_service_uuid_get},
	{"AT+SUUID=",bsp_at_cmd_ble_service_uuid_set},
	{"AT+WUUID?",bsp_at_cmd_ble_write_chara_uuid_get},
	{"AT+RUUID?",bsp_at_cmd_ble_read_chara_uuid_get},
	{"AT+WUUID=",bsp_at_cmd_ble_write_chara_uuid_set},
	{"AT+RUUID=",bsp_at_cmd_ble_read_chara_uuid_set},
	{"AT+OTAWUUID?",bsp_at_cmd_ble_ota_write_chara_uuid_get},
	{"AT+OTARUUID?",bsp_at_cmd_ble_ota_read_chara_uuid_get},
	{"AT+OTAWUUID=",bsp_at_cmd_ble_ota_write_chara_uuid_set},
	{"AT+OTARUUID=",bsp_at_cmd_ble_ota_read_chara_uuid_set},
	{"AT+MAC?",bsp_at_cmd_ble_mac_get},
	{"AT+MAC=",bsp_at_cmd_ble_mac_set},
	{"AT+ADVINT?",bsp_at_cmd_ble_adv_interval_get},
	{"AT+ADVINT=",bsp_at_cmd_ble_adv_interval_set},
	{"AT+ADV?",bsp_at_cmd_ble_adv_status_get},
	{"AT+ADV=",bsp_at_cmd_ble_adv_status_set},
	{"AT+RESE?",bsp_at_cmd_ble_adv_data_get},
	{"AT+RESE=",bsp_at_cmd_ble_adv_data_set},
	{"AT+RESEOFF=",bsp_at_cmd_ble_adv_data_off},
	{"AT+PASSWORD?",bsp_at_cmd_ble_con_password_get},
	{"AT+PASSWORD=",bsp_at_cmd_ble_con_password_set},
	{"AT+PASSWORDC",bsp_at_cmd_ble_con_password_clear},
	{"AT+CONPARAM=",bsp_at_cmd_ble_update_con_param_set},
	{"AT+CONINTMIN?",bsp_at_cmd_ble_min_con_interval_get},
	{"AT+CONINTMIN=",bsp_at_cmd_ble_min_con_interval_set},
	{"AT+CONINTMAX?",bsp_at_cmd_ble_max_con_interval_get},
	{"AT+CONINTMAX=",bsp_at_cmd_ble_max_con_interval_set},
    {"AT+LATENCY?",bsp_at_cmd_ble_slavery_latency_get},
	{"AT+LATENCY=",bsp_at_cmd_ble_slavery_latency_set},
	{"AT+CONTIMEOUT??",bsp_at_cmd_ble_con_timeout_get},
	{"AT+CONTIMEOUT=",bsp_at_cmd_ble_con_timeout_set},
	{"AT+BLESTACK?",bsp_at_cmd_ble_stack_version_get},
	{"AT+ENC=",bsp_at_cmd_encryption_status_set},
	{"AT+RSSI?",bsp_at_cmd_ble_rssi_get},
	{"AT+VDD?",bsp_at_cmd_device_vdd_get},
    {"AT+TEMP?",bsp_at_cmd_device_temp_get},
    {"AT+BLEINIT=",bsp_at_cmd_ble_init_set},
    {"AT+BLEDEINIT",bsp_at_cmd_ble_deinit_set},
    {"AT+BLECONN?",bsp_at_cmd_ble_conn_status_get},
    {"AT+INDICATE=",bsp_at_cmd_ble_indicate_set},
    {"AT+DISCOVERCHAR=",bsp_at_cmd_ble_discover_char_set},
    {"AT+CCCD=",bsp_at_cmd_ble_cccd_set},
    {"AT+BONDLIST?",bsp_at_cmd_ble_bond_list_get},
    {"AT+BONDDEL=",bsp_at_cmd_ble_bond_del_set},
    {"AT+BONDCLEAR",bsp_at_cmd_ble_bond_clear_set},
    {"AT+SCAN=",bsp_at_cmd_ble_scan_status_set},
    {"AT+CONNECT=",bsp_at_cmd_ble_connect_by_addr},
    {"AT+DISCOVER=",bsp_at_cmd_ble_discover_services},
    {"AT+READ=",bsp_at_cmd_ble_read_characteristic},
    {"AT+WRITE=",bsp_at_cmd_ble_write_characteristic},
    {"AT+PAIR",bsp_at_cmd_ble_pairing_start},
    {"AT+NOTIFY=",	bsp_at_cmd_ble_notify_set},
    {"AT+HELP?",bsp_at_cmd_help},
	{NULL,	   bsp_at_cmd_error},
};


void bsp_at_cmd_help(uint8_t *data, uint16_t len)
{
      hsuart_rx_t *hsuart=&hsuart_cb;

     for(uint8_t i = 0; i < sizeof(bsp_at_cmd) / sizeof(bsp_at_cmd[0]); i++) {
           memset(hsuart->txbuf_data,0,sizeof(hsuart->txbuf_data));
           uint8_t *tmp_cmd=(uint8_t *)bsp_at_cmd[i].p_cmd;
           memcpy(hsuart->txbuf_data,tmp_cmd,strlen((char*)tmp_cmd));
           bsp_at_cmd_send_data(hsuart->txbuf_data, strlen((char*)tmp_cmd));
            uint8_t ack_buf[]="\r\n";
            bsp_at_cmd_send_data(ack_buf, sizeof(ack_buf));
           if(bsp_at_cmd[i].p_cmd==NULL){
               break;
           }
     }
}



void bsp_at_cmd_init(void)
{
   memset(&hsuart_cb,0,sizeof(hsuart_cb));
   memset(&at_cmd_cb,0,sizeof(at_cmd_cb));
    memset(&ble_data_cb,0,sizeof(ble_data_cb));

   memset(at_profile_data,0,sizeof(at_profile_data));

   cm_read(&at_cmd_cb,BSP_AT_CMD_CM_PAGE(0),sizeof(at_cmd_cb));
   cm_read(at_profile_data,BLE_PROFILE_CM_PAGE(0),sizeof(at_profile_data));
   if(at_cmd_cb.flash_flag==1){
      memcpy(xcfg_cb.le_addr,at_cmd_cb.ble_addr,6);
      memcpy(xcfg_cb.le_name,at_cmd_cb.ble_name,strlen((char*)at_cmd_cb.ble_name));
      xcfg_cb.rf_tx_pwr=at_cmd_cb.tx_power;
   }
   else{
      memcpy(at_cmd_cb.ble_addr,xcfg_cb.le_addr,6);
      memcpy(at_cmd_cb.ble_name,xcfg_cb.le_name,strlen(xcfg_cb.le_name));
      at_cmd_cb.flash_flag=1;
      at_cmd_cb.wdt_status=ENABLE;
      at_cmd_cb.role_status=ROLE_UNKNOWN;
      at_cmd_cb.echo_status=ENABLE;
      at_cmd_cb.sleep_status=DISABLE;
      at_cmd_cb.uart_baud=115200;
      at_cmd_cb.uart_pack_len=HSUART_TRANSFER_MAX_LEX;
      at_cmd_cb.scan_rsp_status=ENABLE;
      at_cmd_cb.password=123456;
      at_cmd_cb.con_notify_status=ENABLE;
      at_cmd_cb.adv_interval=32;
      at_cmd_cb.min_con_interval=6;
      at_cmd_cb.max_con_interval=12;
      at_cmd_cb.slavery_latency=0;
      at_cmd_cb.con_timeout=300;

   }

   at_cmd_cb.con_handle=0;

   if(at_profile_data[0]==0x0A){
      const u8 *p_data= ble_get_profile_data();  // 修改为const指针
      memcpy((u8*)p_data,at_profile_data,at_profile_data_len_get());  // 强制类型转换
   }
   else{
      const u8 *p_data= ble_get_profile_data();  // 修改为const指针
      memcpy(at_profile_data,(u8*)p_data,at_profile_data_len_get());  // 强制类型转换
   }

   hsuart_cb.tx_done=1;
   //hsuart_transfer_init(at_cmd_cb.uart_baud);

   if(MAX_CM_PAGE<5){
      printf("MAX_CM_PAGE >=5,MAX_CM_PAGE = %d\n",MAX_CM_PAGE);
      while(1);
   }
}



// 测试函数
void bsp_at_cmd_handler(uint16_t msg)
{
   //printf("hsuart111111!\n");
    hsuart_rx_t *hsuart=&hsuart_cb;
    at_cmd_t    *at_cmd=&at_cmd_cb;
    if(MSG_AT_CMD_RX_DONE==msg)
    {
        TRACE("MSG_AT_CMD_RX_DONE\n");

        if (hsuart->rx_len <=1 ) {
            printf("hsuart error!\n");
            return;
        }
        //print_r(hsuart->rxbuf_data, hsuart->rx_len);

        // 去除末尾的\r\n字符，将其替换为\0
        uint16_t actual_len = hsuart->rx_len;
        while (actual_len > 0) {
            if (hsuart->rxbuf_data[actual_len - 1] == '\r' || hsuart->rxbuf_data[actual_len - 1] == '\n') {
                hsuart->rxbuf_data[actual_len - 1] = '\0';
                actual_len--;
            } else {
                break;
            }
        }
        
        // 如果所有字符都是\r\n，则视为无效指令
        if (actual_len == 0) {
            printf("Empty AT command after removing \\r\\n!\n");
            memset(hsuart->rxbuf_data,0,sizeof(hsuart->rxbuf_data));
            return;
        }

        // ---------- 最长前缀匹配逻辑 ----------
        int best_match_idx = -1;
        size_t best_match_len = 0;

        int cmd_count = sizeof(bsp_at_cmd) / sizeof(bsp_at_cmd[0]);

        for (int i = 0; i < cmd_count; i++) {
            // 跳过最后的 NULL 错误处理项，单独处理
            if (bsp_at_cmd[i].p_cmd == NULL) {
                continue;
            }

            size_t cmd_len = strlen(bsp_at_cmd[i].p_cmd);
            // 检查是否前缀匹配（且命令长度不能超过实际接收长度）
            if (cmd_len <= actual_len &&
                strncmp((const char *)hsuart->rxbuf_data, bsp_at_cmd[i].p_cmd, cmd_len) == 0) {
                // 记录长度更长的匹配
                if (cmd_len > best_match_len) {
                    best_match_len = cmd_len;
                    best_match_idx = i;
                }
            }
        }

        // 执行匹配到的命令处理
        if (best_match_idx >= 0) {
            uint8_t *pdata   = &hsuart->rxbuf_data[best_match_len];
            uint16_t data_len = actual_len - best_match_len;
            bsp_at_cmd[best_match_idx].at_cmd_handler(pdata, data_len);
        } else {
            // 没有任何匹配，调用错误处理
            bsp_at_cmd_error(hsuart->rxbuf_data, actual_len);
        }
        memset(hsuart->rxbuf_data,0,sizeof(hsuart->rxbuf_data));

    }else if(MSG_AT_CMD_TX_DONE==msg){
        TRACE("MSG_AT_CMD_TX_DONE\n");
    }
    else if(MSG_BLE_CONNECTED==msg)
    {
       if(at_cmd->notify_enable_status){
          uint8_t tx_buf[]="CONNECT OK\r\n";
          bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
       }

    }else if(MSG_BLE_DISCONNECTED==msg){
        if(at_cmd->notify_enable_status){
          uint8_t tx_buf[]="DISCONNECT\r\n";
          bsp_at_cmd_send_data(tx_buf, sizeof(tx_buf));
        }
    }
    else if(MSG_BLE_TX_DATA==msg){
        ble_data_t *ble_data= &ble_data_cb;
        printf("---->MSG_BLE_TX_DATA\n");
        bsp_at_cmd_send_data(ble_data->ble_rx_data, ble_data->ble_rx_len);

    }




}


// 获取at_cmd结构体指针
at_cmd_t * bsp_at_cmd_get(void)
{
    return &at_cmd_cb;

}