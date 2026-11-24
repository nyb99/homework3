/**
  ******************************************************************************
  * @file    bsp_key.c
  * @brief   Debounced key driver (EXTI event + main-loop confirmation).
  ******************************************************************************
  */
#include "bsp_key.h"

#define KEY_PHASE_IDLE           0u  /* waiting for a falling edge            */
#define KEY_PHASE_PRESS_CHECK    1u  /* edge seen, waiting DEBOUNCE ms        */
#define KEY_PHASE_WAIT_RELEASE   2u  /* press accepted, waiting for release   */
#define KEY_PHASE_RELEASE_CHECK  3u  /* release seen, waiting DEBOUNCE ms     */

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t      pin;
  volatile uint8_t edge_flag;   /* set by the EXTI callback, cleared here */
  uint8_t       phase;
  uint32_t      tick;
} KeyObj_t;

static KeyObj_t s_keys[KEY_ID_COUNT] =
{
  { KEY1_GPIO_Port, KEY1_Pin, 0u, KEY_PHASE_IDLE, 0u },
  { KEY2_GPIO_Port, KEY2_Pin, 0u, KEY_PHASE_IDLE, 0u }
};

static uint8_t s_press[KEY_ID_COUNT];   /* one event waiting for the app */

/**
  * @brief  Reset the key driver. The GPIO / EXTI configuration is done in
  *         MX_GPIO_Init().
  */
void KEY_Init(void)
{
  for (uint8_t i = 0u; i < (uint8_t)KEY_ID_COUNT; i++)
  {
    s_keys[i].edge_flag = 0u;
    s_keys[i].phase     = KEY_PHASE_IDLE;
    s_keys[i].tick      = 0u;
    s_press[i]          = 0u;
  }
}

/**
  * @brief  EXTI callback - called from the interrupt, records the event only.
  * @param  GPIO_Pin pin that generated the interrupt
  */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == KEY1_Pin)
  {
    s_keys[KEY_ID_1].edge_flag = 1u;
  }
  else if (GPIO_Pin == KEY2_Pin)
  {
    s_keys[KEY_ID_2].edge_flag = 1u;
  }
  else
  {
    /* not a key pin - nothing to do */
  }
}

/**
  * @brief  Debounce state machine, call it from the main loop at least every
  *         few milliseconds.
  * @param  now_ms current HAL tick
  */
void KEY_Scan(uint32_t now_ms)
{
  for (uint8_t i = 0u; i < (uint8_t)KEY_ID_COUNT; i++)
  {
    KeyObj_t *key = &s_keys[i];
    /* keys are pulled up and close to GND, so "pressed" means pin low */
    uint8_t pressed = (HAL_GPIO_ReadPin(key->port, key->pin) == GPIO_PIN_RESET) ? 1u : 0u;

    switch (key->phase)
    {
      case KEY_PHASE_IDLE:
        /* a falling edge was captured by the interrupt - start confirming */
        if (key->edge_flag != 0u)
        {
          key->edge_flag = 0u;
          key->tick      = now_ms;
          key->phase     = KEY_PHASE_PRESS_CHECK;
        }
        break;

      case KEY_PHASE_PRESS_CHECK:
        if ((now_ms - key->tick) >= KEY_DEBOUNCE_MS)
        {
          if (pressed != 0u)
          {
            s_press[i]  = 1u;                 /* one press -> one event */
            key->phase  = KEY_PHASE_WAIT_RELEASE;
          }
          else
          {
            key->phase = KEY_PHASE_IDLE;      /* contact bounce, ignore   */
          }
        }
        break;

      case KEY_PHASE_WAIT_RELEASE:
        if (pressed == 0u)
        {
          key->tick  = now_ms;
          key->phase = KEY_PHASE_RELEASE_CHECK;
        }
        break;

      case KEY_PHASE_RELEASE_CHECK:
        if ((now_ms - key->tick) >= KEY_DEBOUNCE_MS)
        {
          key->edge_flag = 0u;                /* drop bounces of the release */
          key->phase = KEY_PHASE_IDLE;
        }
        break;

      default:
        key->phase = KEY_PHASE_IDLE;
        break;
    }
  }
}

/**
  * @brief  Consume the press event of one key (if any).
  * @retval 1 when the key was pressed once since the last call, else 0
  */
uint8_t KEY_TakePress(KeyId_t id)
{
  if ((uint32_t)id >= (uint32_t)KEY_ID_COUNT)
  {
    return 0u;
  }
  if (s_press[id] != 0u)
  {
    s_press[id] = 0u;
    return 1u;
  }
  return 0u;
}
