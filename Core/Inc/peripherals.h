#ifndef __PERIPHERALS_H
#define __PERIPHERALS_H

#include "main.h"

extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

void MX_ADC1_Init(void);
void MX_TIM2_Init(void);
void MX_USART1_UART_Init(void);
void MX_USART2_UART_Init(void);

#endif
