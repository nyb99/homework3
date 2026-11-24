/**
  ******************************************************************************
  * @file    app.c
  * @brief   Application layer of the running light.
  *
  *          One shared state (run/pause, direction, interval, current LED) is
  *          modified by the two keys and by the serial command packets. Every
  *          change is mirrored on the OLED and reported over the serial port.
  *
  *          The whole logic runs in the main loop (App_Loop), the interrupt
  *          callbacks only record events.
  ******************************************************************************
  */
#include "app.h"

#include "bsp_key.h"
#include "bsp_led.h"
#include "oled.h"
#include "protocol.h"
#include "usart.h"

#include <stdio.h>
#include <string.h>

#define APP_DIR_FWD           0u
#define APP_DIR_REV           1u

#define APP_STEP_DEFAULT_MS   300u

/* Command queue drain limit per App_Loop() call: keeps the main loop
   responsive even if the serial line is flooded. */
#define APP_RX_BUDGET         64u

typedef struct
{
  uint8_t  running;         /*!< 0 = pause, 1 = run                          */
  uint8_t  dir;             /*!< APP_DIR_FWD / APP_DIR_REV                   */
  uint16_t step_ms;         /*!< LED switching interval, 100..2000 ms        */
  uint8_t  led;             /*!< index of the lit LED, 0..LED_Count()-1      */
  uint32_t step_tick;       /*!< HAL tick of the last LED step               */
  uint8_t  display_dirty;   /*!< 1 = the OLED has to be updated              */
  uint8_t  feedback_dirty;  /*!< 1 = the serial feedback has to be sent      */
} AppState_t;

static AppState_t     s_app;
static PacketParser_t s_parser;
static char           s_display_cache[OLED_LINE_COUNT][OLED_LINE_CHARS + 1u];

/*---------------------------------------------------------------------------*/
/* Display                                                                   */
/*---------------------------------------------------------------------------*/
/**
  * @brief  Build the four text lines, redraw and refresh only the lines whose
  *         content changed.
  */
static void App_UpdateDisplay(void)
{
  char text[OLED_LINE_COUNT][OLED_LINE_CHARS + 1u];

  (void)snprintf(text[0], sizeof(text[0]), "STATE: %s", (s_app.running != 0u) ? "RUN" : "PAUSE");
  (void)snprintf(text[1], sizeof(text[1]), "DIR: %s", (s_app.dir == APP_DIR_REV) ? "REV" : "FWD");
  (void)snprintf(text[2], sizeof(text[2]), "STEP: %ums", (unsigned int)s_app.step_ms);
  (void)snprintf(text[3], sizeof(text[3]), "LED: %u", (unsigned int)(s_app.led + 1u));

  for (uint8_t line = 0u; line < OLED_LINE_COUNT; line++)
  {
    if (strcmp(text[line], s_display_cache[line]) != 0)
    {
      OLED_ShowString(line, text[line]);
      OLED_RefreshLine(line);
      (void)strncpy(s_display_cache[line], text[line], OLED_LINE_CHARS);
      s_display_cache[line][OLED_LINE_CHARS] = '\0';
    }
  }
}

/*---------------------------------------------------------------------------*/
/* Serial feedback                                                           */
/*---------------------------------------------------------------------------*/
/**
  * @brief  Queue the status line, e.g. STATE=RUN,DIR=FWD,STEP=300,LED=1
  */
static void App_SendFeedback(void)
{
  char message[UART_TX_BUF_SIZE];

  (void)snprintf(message, sizeof(message), "STATE=%s,DIR=%s,STEP=%u,LED=%u\r\n",
                 (s_app.running != 0u) ? "RUN" : "PAUSE",
                 (s_app.dir == APP_DIR_REV) ? "REV" : "FWD",
                 (unsigned int)s_app.step_ms,
                 (unsigned int)(s_app.led + 1u));
  UART_SendText(message);
}

/*---------------------------------------------------------------------------*/
/* Running light                                                             */
/*---------------------------------------------------------------------------*/
/**
  * @brief  Move to the next LED in the current direction and light it.
  */
static void App_AdvanceLed(void)
{
  uint8_t count = LED_Count();

  if (count == 0u)
  {
    return;
  }
  if (s_app.dir == APP_DIR_FWD)
  {
    s_app.led = (uint8_t)((s_app.led + 1u) % count);
  }
  else
  {
    s_app.led = (uint8_t)((s_app.led + count - 1u) % count);
  }
  LED_Show(s_app.led);
}

/*---------------------------------------------------------------------------*/
/* Command packets                                                           */
/*---------------------------------------------------------------------------*/
/**
  * @brief  Apply a validated packet to the shared state.
  */
static void App_ApplyPacket(const Packet_t *packet)
{
  s_app.running  = packet->field.run;
  s_app.dir      = packet->field.dir;
  s_app.step_ms  = Packet_StepMs(packet);
  s_app.step_tick = HAL_GetTick();   /* restart the interval measurement */
  s_app.display_dirty  = 1u;
  s_app.feedback_dirty = 1u;
}

/**
  * @brief  Drain the receive ring buffer and feed the frame parser.
  */
static void App_ProcessSerial(void)
{
  uint8_t  byte;
  uint16_t budget = APP_RX_BUDGET;

  while ((budget > 0u) && (UART_ReadByte(&byte) != 0u))
  {
    PacketResult_t result = PacketParser_Feed(&s_parser, byte);
    budget--;

    if (result == PKT_OK)
    {
      App_ApplyPacket(&s_parser.packet);
    }
    else if (result != PKT_NONE)
    {
      /* frame header, checksum or field invalid -> the command is dropped */
      char message[UART_TX_BUF_SIZE];
      (void)snprintf(message, sizeof(message), "%s\r\n", Packet_ResultText(result));
      UART_SendText(message);
    }
    else
    {
      /* keep assembling */
    }
  }
}

/*---------------------------------------------------------------------------*/
/* Keys                                                                      */
/*---------------------------------------------------------------------------*/
/**
  * @brief  Turn the debounced key events into state changes.
  */
static void App_ProcessKeys(void)
{
  if (KEY_TakePress(KEY_ID_1) != 0u)
  {
    /* KEY1: direction */
    s_app.dir = (s_app.dir == APP_DIR_FWD) ? APP_DIR_REV : APP_DIR_FWD;
    s_app.display_dirty  = 1u;
    s_app.feedback_dirty = 1u;
  }
  if (KEY_TakePress(KEY_ID_2) != 0u)
  {
    /* KEY2: run / pause, the current LED is kept while paused */
    s_app.running = (s_app.running != 0u) ? 0u : 1u;
    s_app.step_tick = HAL_GetTick();
    s_app.display_dirty  = 1u;
    s_app.feedback_dirty = 1u;
  }
}

/*---------------------------------------------------------------------------*/
/* Public API                                                                */
/*---------------------------------------------------------------------------*/
/**
  * @brief  Initialise the application (call once after the HAL peripherals).
  */
void App_Init(void)
{
  memset(&s_app, 0, sizeof(s_app));
  memset(s_display_cache, 0xFF, sizeof(s_display_cache));   /* force redraw */
  PacketParser_Init(&s_parser);

  s_app.running = 1u;
  s_app.dir     = APP_DIR_FWD;
  s_app.step_ms = APP_STEP_DEFAULT_MS;
  s_app.led     = 0u;

  KEY_Init();
  LED_Init();
  LED_Show(s_app.led);

  /* arm the serial reception before the (slow) display initialisation */
  UART_StartReceive();

  OLED_Init();
  s_app.display_dirty = 1u;
  App_UpdateDisplay();

  s_app.step_tick = HAL_GetTick();
  App_SendFeedback();       /* report the power-on state */
  UART_TxTask();
}

/**
  * @brief  Main loop body, call it continuously from main().
  */
void App_Loop(void)
{
  uint32_t now = HAL_GetTick();

  KEY_Scan(now);
  App_ProcessKeys();
  App_ProcessSerial();

  /* running light timing */
  if ((s_app.running != 0u) && (now - s_app.step_tick >= s_app.step_ms))
  {
    s_app.step_tick += s_app.step_ms;
    if (now - s_app.step_tick >= s_app.step_ms)
    {
      s_app.step_tick = now;          /* fell behind, resynchronise */
    }
    App_AdvanceLed();
    s_app.display_dirty = 1u;         /* the LED number on the OLED changes */
  }

  if (s_app.display_dirty != 0u)
  {
    s_app.display_dirty = 0u;
    App_UpdateDisplay();
  }

  if (s_app.feedback_dirty != 0u)
  {
    s_app.feedback_dirty = 0u;
    App_SendFeedback();
  }

  UART_TxTask();
}
