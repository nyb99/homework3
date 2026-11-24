/**
  ******************************************************************************
  * @file    soft_i2c.h
  * @brief   Bit-banged (software) I2C master used by the OLED.
  *
  *          SCL = PB8, SDA = PB9, both configured as open-drain outputs with
  *          the internal pull-up enabled (the 0.96" OLED module also carries
  *          its own 4.7k..10k pull-ups).
  ******************************************************************************
  */
#ifndef __SOFT_I2C_H
#define __SOFT_I2C_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Number of NOPs used as half-bit delay, ~1..2.5 us at 72 MHz depending on
   the optimisation level, i.e. roughly 100..300 kHz SCL. */
#define SOFT_I2C_DELAY_LOOPS  12u

void    SoftI2C_Init(void);
uint8_t SoftI2C_Write(uint8_t dev_addr, const uint8_t *data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif /* __SOFT_I2C_H */
