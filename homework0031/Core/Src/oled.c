/**
  ******************************************************************************
  * @file    oled.c
  * @brief   SSD1306 128x64 OLED driver over the software I2C bus.
  *
  *          The command set used here is the one documented in the SSD1306
  *          datasheet (section 9, "MCU I2C interface" / "Command table").
  *          The refresh strategy (frame buffer + page addressing) follows the
  *          well known 江科大 OLED tutorial, which uses the same SSD1306 chip.
  ******************************************************************************
  */
#include "oled.h"
#include "oledfont.h"
#include "soft_i2c.h"

#include <string.h>

/* Control byte: 0x00 = the following bytes are commands, 0x40 = pixel data. */
#define OLED_CTRL_CMD   0x00u
#define OLED_CTRL_DATA  0x40u

/* 128x64 frame buffer, one byte per 8 vertical pixels (page format). */
static uint8_t s_gram[OLED_PAGE_COUNT][OLED_WIDTH];

/* Buffer used to send one page: control byte + 128 data bytes. */
static uint8_t s_tx_buffer[OLED_WIDTH + 1u];

/**
  * @brief  Send a list of SSD1306 commands in one I2C transaction.
  */
static uint8_t OLED_SendCommands(const uint8_t *commands, uint16_t count)
{
  uint8_t buffer[40];

  if ((commands == 0) || (count == 0u) || (count > (sizeof(buffer) - 1u)))
  {
    return 0u;
  }
  buffer[0] = OLED_CTRL_CMD;
  memcpy(&buffer[1], commands, count);
  return SoftI2C_Write(OLED_I2C_ADDR, buffer, (uint16_t)(count + 1u));
}

/**
  * @brief  Start of an SSD1306 page write: page and column address.
  */
static void OLED_SetPosition(uint8_t page, uint8_t column)
{
  static const uint8_t low_nibble_cmd = 0x00u;
  static const uint8_t high_nibble_cmd = 0x10u;
  uint8_t commands[3];

  commands[0] = (uint8_t)(0xB0u | (page & 0x07u));
  commands[1] = (uint8_t)(low_nibble_cmd | (column & 0x0Fu));
  commands[2] = (uint8_t)(high_nibble_cmd | ((column >> 4) & 0x0Fu));
  (void)OLED_SendCommands(commands, sizeof(commands));
}

/**
  * @brief  Push one page (8 pixel rows) of the frame buffer to the panel.
  */
static void OLED_RefreshPage(uint8_t page)
{
  if (page >= OLED_PAGE_COUNT)
  {
    return;
  }
  OLED_SetPosition(page, 0u);
  s_tx_buffer[0] = OLED_CTRL_DATA;
  memcpy(&s_tx_buffer[1], s_gram[page], OLED_WIDTH);
  (void)SoftI2C_Write(OLED_I2C_ADDR, s_tx_buffer, sizeof(s_tx_buffer));
}

/**
  * @brief  Initialise the panel and clear the screen.
  */
void OLED_Init(void)
{
  /* SSD1306 power-up sequence for a 128x64 panel */
  static const uint8_t init_commands[] =
  {
    0xAEu,              /* display off                                       */
    0xD5u, 0x80u,       /* clock divide ratio / oscillator frequency         */
    0xA8u, 0x3Fu,       /* multiplex ratio = 64 rows                         */
    0xD3u, 0x00u,       /* display offset = 0                                */
    0x40u,              /* start line = 0                                    */
    0x8Du, 0x14u,       /* charge pump enabled (needed for 3.3 V supply)     */
    0x20u, 0x02u,       /* page addressing mode                              */
    0xA1u,              /* segment remap, column 127 -> SEG0                 */
    0xC8u,              /* COM scan direction remapped (top to bottom)       */
    0xDAu, 0x12u,       /* COM pins hardware configuration                   */
    0x81u, 0xCFu,       /* contrast                                          */
    0xD9u, 0xF1u,       /* pre-charge period                                 */
    0xDBu, 0x40u,       /* VCOMH deselect level                              */
    0xA4u,              /* display follows RAM content                       */
    0xA6u,              /* normal (not inverted) display                     */
    0x2Eu,              /* deactivate scrolling                              */
    0xAFu               /* display on                                        */
  };

  SoftI2C_Init();
  (void)OLED_SendCommands(init_commands, sizeof(init_commands));

  OLED_Clear();
  OLED_Refresh();
}

/**
  * @brief  Clear the frame buffer only (call OLED_Refresh to update the panel).
  */
void OLED_Clear(void)
{
  memset(s_gram, 0x00, sizeof(s_gram));
}

/**
  * @brief  Send the whole frame buffer to the panel.
  */
void OLED_Refresh(void)
{
  for (uint8_t page = 0u; page < OLED_PAGE_COUNT; page++)
  {
    OLED_RefreshPage(page);
  }
}

/**
  * @brief  Send one text line (2 pages) of the frame buffer to the panel.
  * @param  line 0..OLED_LINE_COUNT-1
  */
void OLED_RefreshLine(uint8_t line)
{
  if (line >= OLED_LINE_COUNT)
  {
    return;
  }
  OLED_RefreshPage((uint8_t)(line * 2u));
  OLED_RefreshPage((uint8_t)((line * 2u) + 1u));
}

/**
  * @brief  Bitmap of one 8x16 character.
  */
static const uint8_t *OLED_GetGlyph(char character)
{
  uint8_t code = (uint8_t)character;

  if ((code < OLED_FONT_FIRST) || (code > OLED_FONT_LAST))
  {
    code = (uint8_t)'?';
  }
  return &OLED_F8x16[(uint16_t)(code - OLED_FONT_FIRST) * 16u];
}

/**
  * @brief  Draw one line of text (8x16 characters, max 16 characters) into
  *         the frame buffer. The rest of the line is erased.
  * @param  line 0..OLED_LINE_COUNT-1
  */
void OLED_ShowString(uint8_t line, const char *text)
{
  uint8_t page;
  uint8_t column = 0u;

  if ((line >= OLED_LINE_COUNT) || (text == 0))
  {
    return;
  }
  page = (uint8_t)(line * 2u);
  memset(s_gram[page], 0x00, OLED_WIDTH);
  memset(s_gram[page + 1u], 0x00, OLED_WIDTH);

  while ((column < OLED_LINE_CHARS) && (text[column] != '\0'))
  {
    const uint8_t *glyph = OLED_GetGlyph(text[column]);
    uint8_t x = (uint8_t)(column * 8u);

    for (uint8_t i = 0u; i < 8u; i++)
    {
      s_gram[page][x + i]        = glyph[i];
      s_gram[page + 1u][x + i]   = glyph[8u + i];
    }
    column++;
  }
}
