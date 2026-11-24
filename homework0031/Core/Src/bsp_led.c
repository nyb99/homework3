/**
  ******************************************************************************
  * @file    bsp_led.c
  * @brief   LED driver for the 3 running-light LEDs (PA0 / PA1 / PA2).
  ******************************************************************************
  */
#include "bsp_led.h"

#define LED_COUNT  3u

/* Pin of every LED, indexed by the LED number used by the application
   (0 -> LED0 -> PA0, 1 -> LED1 -> PA1, 2 -> LED2 -> PA2). */
static const uint16_t s_led_pin[LED_COUNT] = {LED0_Pin, LED1_Pin, LED2_Pin};

/**
  * @brief  Put the LED driver into a known state (all LEDs off).
  * @note   The GPIO pins themselves are configured in MX_GPIO_Init().
  */
void LED_Init(void)
{
  LED_AllOff();
}

/**
  * @brief  Switch all LEDs off.
  */
void LED_AllOff(void)
{
  HAL_GPIO_WritePin(LED0_GPIO_Port, (uint16_t)(LED0_Pin | LED1_Pin | LED2_Pin), LED_LEVEL_OFF);
}

/**
  * @brief  Light exactly one LED, all the others are switched off.
  * @param  index LED number, 0..LED_COUNT-1 (0 = LED0 = PA0).
  */
void LED_Show(uint8_t index)
{
  if (index >= LED_COUNT)
  {
    return;
  }
  LED_AllOff();
  HAL_GPIO_WritePin(LED0_GPIO_Port, s_led_pin[index], LED_LEVEL_ON);
}

/**
  * @brief  Number of LEDs handled by this driver.
  */
uint8_t LED_Count(void)
{
  return (uint8_t)LED_COUNT;
}
