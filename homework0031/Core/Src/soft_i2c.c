/**
  ******************************************************************************
  * @file    soft_i2c.c
  * @brief   Bit-banged (software) I2C master used by the OLED.
  ******************************************************************************
  */
#include "soft_i2c.h"

#define I2C_SCL_HIGH()  HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_SET)
#define I2C_SCL_LOW()   HAL_GPIO_WritePin(OLED_SCL_GPIO_Port, OLED_SCL_Pin, GPIO_PIN_RESET)
#define I2C_SDA_HIGH()  HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_SET)
#define I2C_SDA_LOW()   HAL_GPIO_WritePin(OLED_SDA_GPIO_Port, OLED_SDA_Pin, GPIO_PIN_RESET)
#define I2C_SDA_READ()  HAL_GPIO_ReadPin(OLED_SDA_GPIO_Port, OLED_SDA_Pin)

#define I2C_ACK   0u
#define I2C_NACK  1u

/**
  * @brief  Half-bit delay of the bit-banged bus.
  */
static void SoftI2C_Delay(void)
{
  for (uint32_t i = 0u; i < SOFT_I2C_DELAY_LOOPS; i++)
  {
    __NOP();
  }
}

/**
  * @brief  Bus idle: both lines released (high).
  * @note   The pins are already configured by MX_GPIO_Init().
  */
void SoftI2C_Init(void)
{
  I2C_SDA_HIGH();
  I2C_SCL_HIGH();
  SoftI2C_Delay();
}

/**
  * @brief  I2C START condition: SDA falls while SCL is high.
  */
static void SoftI2C_Start(void)
{
  I2C_SDA_HIGH();
  SoftI2C_Delay();
  I2C_SCL_HIGH();
  SoftI2C_Delay();
  I2C_SDA_LOW();
  SoftI2C_Delay();
  I2C_SCL_LOW();
  SoftI2C_Delay();
}

/**
  * @brief  I2C STOP condition: SDA rises while SCL is high.
  */
static void SoftI2C_Stop(void)
{
  I2C_SDA_LOW();
  SoftI2C_Delay();
  I2C_SCL_HIGH();
  SoftI2C_Delay();
  I2C_SDA_HIGH();
  SoftI2C_Delay();
}

/**
  * @brief  Send one byte, MSB first.
  * @retval I2C_ACK when the slave acknowledged, I2C_NACK otherwise
  */
static uint8_t SoftI2C_SendByte(uint8_t byte)
{
  uint8_t ack;

  for (uint8_t bit = 0u; bit < 8u; bit++)
  {
    if ((byte & 0x80u) != 0u)
    {
      I2C_SDA_HIGH();
    }
    else
    {
      I2C_SDA_LOW();
    }
    byte <<= 1;
    SoftI2C_Delay();
    I2C_SCL_HIGH();
    SoftI2C_Delay();
    I2C_SCL_LOW();
    SoftI2C_Delay();
  }

  /* 9th clock: release SDA and sample the acknowledge bit */
  I2C_SDA_HIGH();
  SoftI2C_Delay();
  I2C_SCL_HIGH();
  SoftI2C_Delay();
  ack = (I2C_SDA_READ() == GPIO_PIN_SET) ? I2C_NACK : I2C_ACK;
  I2C_SCL_LOW();
  SoftI2C_Delay();

  return ack;
}

/**
  * @brief  Write a block of bytes to a slave.
  * @param  dev_addr 7 bit device address (not shifted)
  * @param  data     bytes to send
  * @param  length   number of bytes
  * @retval 1 on success, 0 when the slave did not acknowledge
  */
uint8_t SoftI2C_Write(uint8_t dev_addr, const uint8_t *data, uint16_t length)
{
  uint8_t ok = 1u;

  SoftI2C_Start();
  if (SoftI2C_SendByte((uint8_t)(dev_addr << 1)) != I2C_ACK)
  {
    ok = 0u;
  }
  for (uint16_t i = 0u; (ok != 0u) && (i < length); i++)
  {
    if (SoftI2C_SendByte(data[i]) != I2C_ACK)
    {
      ok = 0u;
    }
  }
  SoftI2C_Stop();

  return ok;
}
