#include "include.h"
#include "ble_client.h"
#include "ble_user_service.h"
#include "bsp_at_cmd.h"
#include "api_msg.h"

typedef enum {
    STA_IDLE,
    STA_W4_ENCRYPTED_CONNECTION,
    STA_W4_SERVICE_RESULT,
    STA_W4_CHARACTERISTIC_RESULT,
    STA_W4_DATA_SOURCE_SUBSCRIBED,
    STA_W4_NOTIFICATION_SOURCE_SUBSCRIBED,
    STA_SUBSCRIBED,
    STA_W4_DISCONNECT
} client_state_t;

AT(.com_rodata.svc_info)
static server_info_t server_info[MAX_BLE_CONNECTIONS];

static att_client_handler_t  client_handler;

static client_state_t tc_state[MAX_BLE_CONNECTIONS];

server_info_t *ble_get_server_info(uint8_t conidx)
{
    return &server_info[conidx];
}

void ble_client_event_callback(uint8_t event_type, uint8_t *packet, uint16_t size)
{
    uint8_t conidx = 0;
    static uint8_t last_event_type = 0;
    if(event_type != last_event_type)
    {
        printf("%s, 0x%x\n", __func__, event_type);
        last_event_type = event_type;
    }
    
    switch(event_type){
        case BLE_EVT_CONNECT: {
            uint8_t status = packet[10];
            uint8_t role = packet[9];
            /*if peer device is master, there is no need to discover services*/
            if (role == ROLE_SLAVE) {
                printf("BLE_EVT_DISCONNECT role == ROLE_SLAVE\n");
                return;
            }

            if (status) {
                printf("%s, connection failed\n", __func__);
                return;
            }

            uint16_t con_handle;

            memcpy(&con_handle, &packet[7], 2);

            conidx = ble_get_free_conidx(con_handle);

            printf("%s BLE_EVT_CONNECT, %s\n", __func__, packet[9] ?  "ROLE_SLAVE": "ROLE_MASTER");

            if (conidx != INVALID_CONIDX) {
                ble_cb_t *ble_cb_node = ble_get_ble_info();

                memcpy(&ble_cb_node[conidx].conhdl, &packet[7], 2);

                ble_cb_node[conidx].role = packet[9];
                ble_conn_state_set(1, conidx, packet[9]);
                printf("-->conhdl %x, conidx %x\n",  ble_cb_node[conidx].conhdl, conidx);

                memcpy(&server_info[conidx].conn_handle, &packet[7], 2);

                printf("server_info.conn_handle:%x\n",server_info[conidx].conn_handle);
                tc_state[conidx] = STA_W4_SERVICE_RESULT;
                server_info[conidx].service_cnt = 0;

                // 添加：更新AT指令结构体中的连接句柄并发送连接消息
                at_cmd_t*at_cmd = bsp_at_cmd_get();
                at_cmd->con_handle = server_info[conidx].conn_handle;
                at_cmd->role_status = ROLE_MASTER;
                msg_enqueue(MSG_BLE_CONNECTED);
                uint8_t ret = ble_client_discover_primary_services(server_info[conidx].conn_handle);
                printf("sdp result:%x\n", ret);

                char urc_buf[30] = {0};
                int pos = sprintf(urc_buf, "CONNECT OK!");

                // 手动添加 \r\n
                pos += sprintf(&urc_buf[pos], "\r\n");
                bsp_at_cmd_send_data((uint8_t*)urc_buf, (uint16_t)pos);
            } else {
                printf("%s conidx(%d) must less than (%d)\n", conidx, MAX_BLE_CONNECTIONS);
            }
        } return;

        case BLE_EVT_DISCONNECT:{
            uint16_t con_handle;
            uint8_t disc_reason = packet[2];
            memcpy(&con_handle, &packet[0], 2);

            conidx = ble_get_conidx(con_handle);

            if (conidx == INVALID_CONIDX) {
                return;
            }

            uint8_t role = ble_get_role(conidx);
            /*if peer device is master, ignore this event*/
            if (role == ROLE_SLAVE) {
                printf("BLE_EVT_DISCONNECT role == ROLE_SLAVE\n");
                return;
            }
            printf("%s BLE_EVT_DISCONNECT, role %x, conidx %d, conhdl %x, disc_reason %x\n", __func__, role, conidx, con_handle, disc_reason);

            ble_cb_t *ble_cb_node = ble_get_ble_info();

            ble_conn_state_set(0, conidx, ROLE_UNKNOWN);

            ble_cb_node[conidx].conhdl = 0;
            ble_cb_node[conidx].role = ROLE_UNKNOWN;

            server_info[conidx].conn_handle = 0;
            at_cmd_t*at_cmd = bsp_at_cmd_get();
            at_cmd->con_handle = 0;
            at_cmd->role_status = ROLE_UNKNOWN;
            msg_enqueue(MSG_BLE_DISCONNECTED);

            char urc_buf[30] = {0};
            int pos = sprintf(urc_buf, "DISCONNECT OK!");

            // 手动添加 \r\n
            pos += sprintf(&urc_buf[pos], "\r\n");
            bsp_at_cmd_send_data((uint8_t*)urc_buf, (uint16_t)pos);
            return;
        }
        case BLE_EVT_CONNECT_PARAM_UPDATE_DONE:
            printf("BLE_EVT_CONNECT_PARAM_UPDATE_DONE\n");
            return;

        case BLE_EVT_ADV_REPORT: {
            adv_report_t adv_report;
            adv_report.offset = 0;
            memcpy(&adv_report, packet, 10);
            adv_report.report = &packet[10];

            // 调用原有的回调处理函数（不可遗漏）
            ble_advertising_report_callback(adv_report);

            //打印太频繁，不上报
            // 构造 MAC 地址字符串
            char addr_str[13];
            sprintf(addr_str, "%02X%02X%02X%02X%02X%02X",
                    adv_report.addr[5], adv_report.addr[4], adv_report.addr[3],
                    adv_report.addr[2], adv_report.addr[1], adv_report.addr[0]);

            // 构造上报内容：+SCAN:addr_type,addr,rssi,hex_data
            char urc_buf[256];
            int pos = sprintf(urc_buf, "+SCAN:%d,%s,%d,",
                            adv_report.addr_type, addr_str, adv_report.rssi);

            // // 附加广播数据（十六进制）
            for (int i = 0; i < adv_report.report_len; i++) {
                if ((pos + 2) >= (int)sizeof(urc_buf) - 3) break; // 为 \r\n\0 留空间
                pos += sprintf(&urc_buf[pos], "%02X", adv_report.report[i]);
            }

            // 手动添加 \r\n
            pos += sprintf(&urc_buf[pos], "\r\n");
            bsp_at_cmd_send_data((uint8_t*)urc_buf, (uint16_t)pos);

        } return;

        default:
            break;
    }

    uint16_t conhdl = (packet[0] | packet[1] << 8);
    conidx = ble_get_conidx(conhdl);
    server_info_t *p_svc_info = &server_info[conidx];

    switch(tc_state[conidx]){
        case STA_W4_SERVICE_RESULT:
            switch(event_type){
                case BLE_EVT_SERVICE_FIND_RESULT:
                    printf("BLE_EVT_SERVICE_FIND_RESULT\n");
                    if(p_svc_info->service_cnt >= MAX_SERVICE_NUM){
                        printf("error: server_info.service_cnt >= MAX_SERVICE_NUM\n");
                    }else{
                        ble_service_query_result_get_service(packet, &p_svc_info->service[p_svc_info->service_cnt++]);
                        // URC 上报：+GATTSERVICE:start_handle,uuid16
                        char urc_buf[64];
                        int len = sprintf(urc_buf, "+GATTSERVICE:0x%X, 0x%04X\r\n", 
                                        p_svc_info->service[p_svc_info->service_cnt-1].start_group_handle, 
                                        p_svc_info->service[p_svc_info->service_cnt-1].uuid16);
                        bsp_at_cmd_send_data((uint8_t*)urc_buf, (uint16_t)len);
                    }
                    break;

                case BLE_EVT_PROFILE_FIND_DONE:
                    p_svc_info->service_index = 0;
                    p_svc_info->characteristic_index = 0;

                    if(p_svc_info->service_cnt != 0){
                        printf("All service discover complete:%d\n", p_svc_info->service_cnt);
                        for(uint8_t cnt = 0; cnt < p_svc_info->service_cnt; cnt++){
                            printf("service[%d]->uuid16:%04x,start handle:%04x,stop handle:%04x\n",cnt, p_svc_info->service[cnt].uuid16, \
                                        p_svc_info->service[cnt].start_group_handle, p_svc_info->service[cnt].end_group_handle);
                        }
                        printf("Client Discovery characteristic start\n");
                        tc_state[conidx] = STA_W4_CHARACTERISTIC_RESULT;
                        p_svc_info->characteristic_cnt = 0;
                        ble_client_discover_characteristics_for_service(p_svc_info->conn_handle, &p_svc_info->service[0]);
                    }
                    else{
                        tc_state[conidx] = STA_IDLE;
                        p_svc_info->service_cnt = 0;
                        p_svc_info->characteristic_cnt = 0;
                        printf("Client Discovery service failed->No service\n");
                    }
                    break;

                default:
                    break;
            }
            break;

        case STA_W4_CHARACTERISTIC_RESULT:
            switch(event_type){
                case BLE_EVT_CHARACTERISTIC_FIND_RESULT:
                    printf(" BLE_EVT_CHARACTERISTIC_FIND_RESULT characteristic_cnt %d\n", p_svc_info->characteristic_cnt);
                    if(p_svc_info->characteristic_cnt >= MAX_CHARACTERISTIC_NUM){
                        printf("error: server_info.characteristic_cnt >= MAX_CHARACTERISTIC_NUM\n");
                    }else{
                        ble_characteristic_query_result_get_characteristic(packet, &p_svc_info->characteristic[p_svc_info->characteristic_cnt]);
                        // URC 上报：+GATTCHAR:value_handle,uuid16,properties
                        char urc_buf[64];
                        int len = sprintf(urc_buf, "+GATTCHAR:0x%X,0x%04X,0x%02X\r\n", 
                                        p_svc_info->characteristic[p_svc_info->characteristic_cnt].value_handle, 
                                        p_svc_info->characteristic[p_svc_info->characteristic_cnt].uuid16, 
                                        p_svc_info->characteristic[p_svc_info->characteristic_cnt].properties);
                        bsp_at_cmd_send_data((uint8_t*)urc_buf, (uint16_t)len);
                        p_svc_info->characteristic_cnt++;
                    }
                    break;

                case BLE_EVT_PROFILE_FIND_DONE:
                    if (++p_svc_info->service_index < p_svc_info->service_cnt) {
                        uint8_t svc_idx = p_svc_info->service_index;
                        tc_state[conidx] = STA_W4_CHARACTERISTIC_RESULT;
                        ble_client_discover_characteristics_for_service(p_svc_info->conn_handle, &p_svc_info->service[svc_idx]);
                    } else {
                        uint16_t config;
                        while(p_svc_info->characteristic_index < p_svc_info->characteristic_cnt) {
                            uint8_t charc_idx = p_svc_info->characteristic_index;
                            if(p_svc_info->characteristic[charc_idx].properties & (ATT_PROPERTY_NOTIFY | ATT_PROPERTY_INDICATE)){
                                printf("notify uuid:%04x\n", p_svc_info->characteristic[p_svc_info->characteristic_index].uuid16);
                                if(p_svc_info->characteristic[p_svc_info->characteristic_index].properties & ATT_PROPERTY_NOTIFY){
                                    config = GATT_CCC_NOTIFICATION;
                                }else{
                                    config = GATT_CCC_INDICATION;
                                }
                                ble_client_write_client_characteristic_configuration(p_svc_info->conn_handle, &p_svc_info->characteristic[p_svc_info->characteristic_index], config);
                                p_svc_info->characteristic_index++;
                                tc_state[conidx] = STA_W4_CHARACTERISTIC_RESULT;
                                return;
                            }
                            p_svc_info->characteristic_index++;
                        }
                        tc_state[conidx] = STA_SUBSCRIBED;
                    }
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static uint16_t ble_client_notify_callback(uint16_t con_handle, uint16_t attribute_handle, uint8_t *buffer, uint16_t buffer_size)
{
    // 预留足够空间：前缀 + 4+4+逗号 + 每字节2字符 + \r\n + \0
    // 假设最大buffer_size为512（MTU上限），则最大长度约为 20 + 1024 + 2 ≈ 1100 字节
    // 构造上报内容：+NOTIFY:con_handle,attr_handle,hex_data
    char urc_buf[1150];
    int pos = sprintf(urc_buf, "+NOTIFY:0x%04X,0x%04X,", con_handle, attribute_handle);

    for (uint16_t i = 0; i < buffer_size; i++) {
        if ((pos + 2) >= (int)sizeof(urc_buf) - 3) break;
        pos += sprintf(&urc_buf[pos], "%02X", buffer[i]);
    }

    // 手动添加 \r\n
    pos += sprintf(&urc_buf[pos], "\r\n");
    bsp_at_cmd_send_data((uint8_t*)urc_buf, (uint16_t)pos);

    printf("ble_client_notify_callback\n");
    print_r(buffer, buffer_size);

    return 0;
}

static uint16_t ble_att_read_callback(uint16_t con_handle, uint16_t attribute_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size)
{
    // 构造上报：+GATTREAD:con_handle,attr_handle,hex_data
    char urc_buf[256];
    int pos = sprintf(urc_buf, "+GATTREAD:0x%04X,0x%04X,", con_handle, attribute_handle);
    for (uint16_t i = 0; i < buffer_size; i++) {
        if ((pos + 2) >= (int)sizeof(urc_buf) - 3) break;
        pos += sprintf(&urc_buf[pos], "%02X", buffer[i]);
    }
    // 手动添加 \r\n
    pos += sprintf(&urc_buf[pos], "\r\n");
    bsp_at_cmd_send_data((uint8_t*)urc_buf, (uint16_t)pos);
    printf("attribute_handle:%x\n",attribute_handle);

    print_r(buffer, buffer_size);

    return 0;
}

void ble_client_init(void)
{
    memset(tc_state, STA_IDLE, sizeof(tc_state));

    client_handler.event_handler = ble_client_event_callback;
    client_handler.notify_callback = ble_client_notify_callback;
    client_handler.read_callback = ble_att_read_callback;

    att_client_register_handler(&client_handler);
}

