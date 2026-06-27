#ifndef __APP_H
#define __APP_H

#include "main.h"

typedef enum
{
  SYS_STANDBY = 0,
  SYS_LIGHTING,
  SYS_OVERHEAT
} SystemState_t;

void App_Init(void);
void App_Loop(void);
void App_OnGpioInterrupt(uint16_t gpio_pin);
void App_OnTimerInterrupt(TIM_HandleTypeDef *htim);
void App_OnBluetoothByte(uint8_t received_byte);
void App_HandleBluetoothCommand(const char *command);

#endif
