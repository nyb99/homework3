/**
  ******************************************************************************
  * @file    bsp_key.h
  * @brief   Debounced key driver.
  *
  *          KEY1 = PB11 (EXTI11)  -> direction  (forward / reverse)
  *          KEY2 = PB1  (EXTI1)   -> run      (run / pause)
  *
  *          The EXTI interrupt only records "a falling edge happened"; the
  *          debounce state machine runs in the main loop (KEY_Scan) and emits
  *          exactly one event per physical press.
  ******************************************************************************
  */
#ifndef __BSP_KEY_H
#define __BSP_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define KEY_DEBOUNCE_MS  20u

typedef enum
{
  KEY_ID_1 = 0,   /* KEY1 -> PB11 */
  KEY_ID_2 = 1,   /* KEY2 -> PB1  */
  KEY_ID_COUNT
} KeyId_t;

void    KEY_Init(void);
void    KEY_Scan(uint32_t now_ms);
uint8_t KEY_TakePress(KeyId_t id);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_KEY_H */
