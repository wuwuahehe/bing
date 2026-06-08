#include "include.h"
#include "driver_com.h"
#include "driver_hsuart.h"
#include "driver_gpio.h"
#include "hsuart_transfer.h"
#include "bsp_at_cmd.h"

#define HSUART_TX_PORT              GPIOA_REG
#define HSUART_TX_PIN               GPIO_PIN_4
#define HSUART_RX_PORT              GPIOA_REG
#define HSUART_RX_PIN               GPIO_PIN_3
#define HSUART_KEY_PORT             GPIOA_REG
#define HSUART_KEY_PIN              GPIO_PIN_7

#define RECV_DMA_BUF_MAX_LEN        64
static uint8_t hsuart_dma_rx_buf[RECV_DMA_BUF_MAX_LEN];
static uint8_t hsuart_dma_tx_buf[RECV_DMA_BUF_MAX_LEN];
static uint8_t hsuart_user_buf[RECV_DMA_BUF_MAX_LEN];
static uint8_t hsuart_user_buf_len = 0;

void hsuart_dma_tx(const uint8_t *dma_buf, uint8_t dma_buf_len)
{
    printf("hsuart_dma_tx, len=%d\n", dma_buf_len);
    printf("Data: ");
    for(int i=0; i<dma_buf_len; i++) {
        printf("0x%02X ", dma_buf[i]);
    }
    printf("\n");
    memcpy(hsuart_dma_tx_buf, dma_buf, dma_buf_len);
    hsuart_dma_start(HSUT_TRANSMIT, DMA_ADR(hsuart_dma_tx_buf), dma_buf_len);
    while (hsuart_get_flag(HSUART_IT_TX) == RESET);
}


AT(.com_periph.hsuart.isr)
void hsuart_dma_rx_isr()
{
    //at_cmd_t  *at_cmd =&at_cmd_cb;
    if(hsuart_get_flag(HSUART_IT_RX)){
        //hsuart_rx_flag = 1;
        hsuart_user_buf_len = hsuart_get_fifo_counter();
        memcpy(hsuart_user_buf, hsuart_dma_rx_buf, hsuart_user_buf_len);
        bsp_at_cmd_rx_data(hsuart_user_buf,hsuart_user_buf_len);
        memset(hsuart_user_buf,0,sizeof(hsuart_user_buf));
        hsuart_clear_flag(HSUART_IT_RX | HSUART_IT_RX_TMR_OV);
        hsuart_dma_start(HSUT_RECEIVE, DMA_ADR(hsuart_dma_rx_buf), RECV_DMA_BUF_MAX_LEN);

    }
}


// 初始化HSUART
void hsuart_transfer_init(u32 baud)
{
    gpio_init_typedef gpio_init_structure;
    hsuart_init_typedef hsuart_init_struct;

    clk_gate0_cmd(CLK_GATE0_HSUART, CLK_EN);

    // Rx
    gpio_init_structure.gpio_pin = HSUART_RX_PIN;
    gpio_init_structure.gpio_dir = GPIO_DIR_INPUT;
    gpio_init_structure.gpio_fen = GPIO_FEN_PER;
    gpio_init_structure.gpio_fdir = GPIO_FDIR_SELF;
    gpio_init_structure.gpio_mode = GPIO_MODE_DIGITAL;
    gpio_init_structure.gpio_pupd = GPIO_PUPD_PU10K;
    gpio_init(HSUART_RX_PORT, &gpio_init_structure);

    // Tx
    gpio_init_structure.gpio_pin = HSUART_TX_PIN;
    gpio_init_structure.gpio_dir = GPIO_DIR_OUTPUT;
    gpio_init_structure.gpio_drv = GPIO_DRV_6MA;
    gpio_init(HSUART_TX_PORT, &gpio_init_structure);

    // IO Key
    gpio_init_structure.gpio_pin = HSUART_KEY_PIN;
    gpio_init_structure.gpio_dir = GPIO_DIR_INPUT;
    gpio_init_structure.gpio_fen = GPIO_FEN_GPIO;
    gpio_init_structure.gpio_fdir = GPIO_FDIR_SELF;
    gpio_init_structure.gpio_mode = GPIO_MODE_DIGITAL;
    gpio_init_structure.gpio_pupd = GPIO_PUPD_PU10K;
    gpio_init(HSUART_KEY_PORT, &gpio_init_structure);

    gpio_func_mapping_config(HSUART_TX_PORT, HSUART_TX_PIN, GPIO_CROSSBAR_OUT_HURTX);
    gpio_func_mapping_config(HSUART_RX_PORT, HSUART_RX_PIN, GPIO_CROSSBAR_IN_HURRX);

    hsuart_init_struct.baud = baud;
    hsuart_init_struct.tx_mode = HSUT_TX_DMA_MODE;
    hsuart_init_struct.rx_mode = HSUT_RX_DMA_MODE;
    hsuart_init_struct.rx_dma_buf_mode = HSUT_DMA_BUF_LOOP;
    hsuart_init_struct.tx_stop_bit = HSUT_STOP_BIT_1BIT;
    hsuart_init_struct.tx_word_len = HSUT_TX_LENGTH_8b;
    hsuart_init_struct.rx_word_len = HSUT_RX_LENGTH_8b;
    hsuart_init_struct.clk_source = HSUT_24M_CLK;   // When selecting clock PLLDIV4, ensure that the sys_clk is SYS_60M or higher.
    hsuart_init_struct.one_line_en = HSUT_ONELINE_DIS;
    hsuart_init(&hsuart_init_struct);

    hsuart_cmd(HSUT_TRANSMIT | HSUT_RECEIVE, ENABLE);

    hsuart_dma_start(HSUT_RECEIVE, DMA_ADR(hsuart_dma_rx_buf), RECV_DMA_BUF_MAX_LEN);

	hsuart_pic_config(hsuart_dma_rx_isr, 0, HSUART_IT_RX | HSUART_IT_RX_TMR_OV, ENABLE);

	hsuart_rx_idle_config(50, ENABLE);  // 空闲中断
}

// 测试函数
void hsuart_transfer_example(void)
{
    #if 0
    u8 len;
    u8 recv_buf[10];
    static u8 key_allow = 1;
//    static u32 delay_cnt;

    if (gpio_read_bit(HSUART_KEY_PORT, HSUART_KEY_PIN) == RESET) {
        if (key_allow) {
            key_allow = 0;

            printf("\nkey_down~\n");
            len = hsuart_get_fifo_counter();
            if (len) {
                hsuart_receive_data_from_fifo(recv_buf, len);
                printf("receive data from fifo[%d]: ", len);
                print_r(recv_buf, len);

                hsuart_dma_start(HSUT_TRANSMIT, (uint32_t)recv_buf, len);
                while (hsuart_get_flag(HSUART_IT_TX) == RESET);
            }
        }
    } else {
        key_allow = 1;
    }

//    if (tick_check_expire(delay_cnt, 1000)) {
//        delay_cnt = tick_get();
//        printf("fifo_cnt: %d  dma_buf: ", hsuart_get_fifo_counter());
//        print_r(receive_dma_buf, RECV_DMA_BUF_MAX_LEN);
//    }
//	(void*)delay_cnt;
    #endif
}
