#ifndef _HSUART_TRANSFER_H
#define _HSUART_TRANSFER_H


void hsuart_transfer_init(u32 baud);
void hsuart_transfer_example(void);
void hsuart_dma_tx(const uint8_t *dma_buf, uint8_t dma_buf_len);
#endif // _HSUART_TRANSFER_H
