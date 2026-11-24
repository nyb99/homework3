/**
  ******************************************************************************
  * @file    bsp_led.h
  * @brief   LED driver for the 3 running-light LEDs (PA0 / PA1 / PA2).
  ******************************************************************************
  * @attention
  *
  * The LEDs are wired active-high on the training board: driving the pin high
  * lights the LED, driving it low turns it off. If your board is wired the
  * other way round, change LED_LEVEL_ON to GPIO_PIN_RESET - nothing else has
  * to be modified.
  ******************************************************************************
  */
#ifndef __BSP_LED_H
#define __BSP_LED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define LED_LEVEL_ON   GPIO_PIN_SET
#define LED_LEVEL_OFF  GPIO_PIN_RESET

void    LED_Init(void);
void    LED_AllOff(void);
void    LED_Show(uint8_t index);
uint8_t LED_Count(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LED_H */
