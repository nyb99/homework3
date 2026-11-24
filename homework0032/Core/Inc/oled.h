/**
  ******************************************************************************
  * @file    oled.h
  * @brief   SSD1306 128x64 OLED driver over the hardware I2C1 peripheral
  *          (PB6 = SCL, PB7 = SDA, 400 kHz).
  *
  *          The driver keeps a 128x64 frame buffer in RAM and pushes it to the
  *          panel page by page, so a single text line can be refreshed on its
  *          own (2 pages) instead of rewriting the whole screen.
  ******************************************************************************
  */
#ifndef __OLED_H
#define __OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "oledfont.h"

#define OLED_I2C_ADDR     0x3Cu   /* 7 bit address of the usual 0.96" module  */

#define OLED_WIDTH        128u
#define OLED_HEIGHT       64u
#define OLED_PAGE_COUNT   (OLED_HEIGHT / 8u)                 /* 8 pages      */
#define OLED_LINE_COUNT   (OLED_HEIGHT / OLED_FONT_HEIGHT)   /* 4 text lines */
#define OLED_LINE_CHARS   (OLED_WIDTH / 8u)                  /* 16 chars     */

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Refresh(void);
void OLED_RefreshLine(uint8_t line);
void OLED_ShowString(uint8_t line, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H */
