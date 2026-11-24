/**
  ******************************************************************************
  * @file    usart.h
  * @brief   USART1 (PA9 = TX, PA10 = RX, 115200 8N1) transport layer.
  *
  *          * reception runs in interrupt mode, the callback only stores the
  *            received byte into a ring buffer,
  *          * feedback is sent with HAL_UART_Transmit_DMA() (DMA1 channel 4),
  *            the caller never blocks; when a new message arrives while a
  *            transmission is running, the newest message is queued and sent
  *            right after it.
  ******************************************************************************
  */
#ifndef __USART_H
#define __USART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define UART_RX_BUF_SIZE   128u   /*!< receive ring buffer, power of two       */
#define UART_TX_BUF_SIZE   64u    /*!< longest feedback message, incl. '\0'    */

extern UART_HandleTypeDef huart1;
extern DMA_HandleTypeDef hdma_usart1_tx;

void     MX_USART1_UART_Init(void);

void     UART_StartReceive(void);
uint8_t  UART_ReadByte(uint8_t *byte);
uint32_t UART_GetDroppedCount(void);

void     UART_SendText(const char *text);
void     UART_TxTask(void);

#ifdef __cplusplus
}
#endif

#endif /* __USART_H */
