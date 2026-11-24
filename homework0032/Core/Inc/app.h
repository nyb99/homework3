/**
  ******************************************************************************
  * @file    app.h
  * @brief   Application layer: one shared running-light state driven by the
  *          keys, the serial protocol and shown on the OLED.
  ******************************************************************************
  */
#ifndef __APP_H
#define __APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void App_Init(void);
void App_Loop(void);

#ifdef __cplusplus
}
#endif

#endif /* __APP_H */
