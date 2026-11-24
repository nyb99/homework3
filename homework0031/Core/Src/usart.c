/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <string.h>
/* USER CODE END 0 */

UART_HandleTypeDef huart1;

/* USART1 init function */

void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspInit 0 */

  /* USER CODE END USART1_MspInit 0 */
    /* USART1 clock enable */
    __HAL_RCC_USART1_CLK_ENABLE();

    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* USART1 interrupt Init */
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspInit 1 */

  /* USER CODE END USART1_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART1)
  {
  /* USER CODE BEGIN USART1_MspDeInit 0 */

  /* USER CODE END USART1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART1_CLK_DISABLE();

    /**USART1 GPIO Configuration
    PA9     ------> USART1_TX
    PA10     ------> USART1_RX
    */
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);

    /* USART1 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  /* USER CODE BEGIN USART1_MspDeInit 1 */

  /* USER CODE END USART1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */

/*---------------------------------------------------------------------------*/
/* Receive path: interrupt -> ring buffer -> main loop                       */
/*---------------------------------------------------------------------------*/
static uint8_t           s_rx_byte;                          /* HAL RX target  */
static uint8_t           s_rx_buffer[UART_RX_BUF_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint32_t s_rx_dropped;

/*---------------------------------------------------------------------------*/
/* Transmit path: main loop -> HAL_UART_Transmit_IT()                        */
/*---------------------------------------------------------------------------*/
static char              s_tx_active[UART_TX_BUF_SIZE];      /* being sent    */
static char              s_tx_next[UART_TX_BUF_SIZE];        /* queued        */
static volatile uint16_t s_tx_active_len;
static volatile uint16_t s_tx_next_len;
static volatile uint8_t  s_tx_busy;
static volatile uint8_t  s_tx_pending;

/**
  * @brief  Start the interrupt driven reception (call once after MX init).
  */
void UART_StartReceive(void)
{
  s_rx_head    = 0u;
  s_rx_tail    = 0u;
  s_rx_dropped = 0u;
  if (HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1u) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief  Take one received byte out of the ring buffer (main loop side).
  * @retval 1 when a byte was returned, 0 when the buffer is empty
  */
uint8_t UART_ReadByte(uint8_t *byte)
{
  if ((byte == 0) || (s_rx_tail == s_rx_head))
  {
    return 0u;
  }
  *byte = s_rx_buffer[s_rx_tail];
  s_rx_tail = (uint16_t)((s_rx_tail + 1u) % UART_RX_BUF_SIZE);
  return 1u;
}

/**
  * @brief  Number of bytes lost because the ring buffer was full.
  */
uint32_t UART_GetDroppedCount(void)
{
  return s_rx_dropped;
}

/**
  * @brief  Queue a text message for interrupt driven transmission.
  * @note   Call it from the main loop only. Only the newest message is kept
  *         when the previous one is still being transmitted.
  */
void UART_SendText(const char *text)
{
  if (text == 0)
  {
    return;
  }
  (void)strncpy(s_tx_next, text, UART_TX_BUF_SIZE - 1u);
  s_tx_next[UART_TX_BUF_SIZE - 1u] = '\0';
  s_tx_next_len = (uint16_t)strlen(s_tx_next);
  s_tx_pending  = 1u;
}

/**
  * @brief  Start the queued transmission when the UART is free (main loop).
  */
void UART_TxTask(void)
{
  if ((s_tx_busy != 0u) || (s_tx_pending == 0u))
  {
    return;
  }
  if (s_tx_next_len == 0u)
  {
    s_tx_pending = 0u;
    return;
  }
  (void)memcpy(s_tx_active, s_tx_next, s_tx_next_len);
  s_tx_active_len = s_tx_next_len;
  s_tx_pending    = 0u;
  s_tx_busy       = 1u;
  if (HAL_UART_Transmit_IT(&huart1, (uint8_t *)s_tx_active, s_tx_active_len) != HAL_OK)
  {
    s_tx_busy    = 0u;
    s_tx_pending = 1u;   /* try again on the next call */
  }
}

/**
  * @brief  Reception complete interrupt: store the byte and re-arm.
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance != USART1)
  {
    return;
  }
  {
    uint16_t next = (uint16_t)((s_rx_head + 1u) % UART_RX_BUF_SIZE);
    if (next != s_rx_tail)
    {
      s_rx_buffer[s_rx_head] = s_rx_byte;
      s_rx_head = next;
    }
    else
    {
      s_rx_dropped++;
    }
  }
  (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1u);
}

/**
  * @brief  Transmission complete interrupt: only report that the line is free.
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    s_tx_busy = 0u;
  }
}

/**
  * @brief  UART error interrupt (overrun, framing, ...): clear it and re-arm
  *         the reception so that the link recovers by itself.
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    __HAL_UART_CLEAR_OREFLAG(huart);
    (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1u);
  }
}

/* USER CODE END 1 */
