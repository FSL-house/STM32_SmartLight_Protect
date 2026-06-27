/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* GPIO 引脚与 .ioc 中的用户标签保持一致。 */
#define BUZZER_GPIO_PORT              GPIOC
#define BUZZER_GPIO_PIN               GPIO_PIN_6
#define BUZZER_OFF_LEVEL              GPIO_PIN_RESET
#define LAMP_ENABLE_GPIO_PORT         GPIOD
#define LAMP_ENABLE_GPIO_PIN          GPIO_PIN_14
#define LAMP_ENABLE_OFF_LEVEL         GPIO_PIN_SET
#define PIR_GPIO_PORT                 GPIOA
#define PIR_GPIO_PIN                  GPIO_PIN_12
#define KEY3_UNUSED_GPIO_PORT         GPIOC
#define KEY3_UNUSED_GPIO_PIN          GPIO_PIN_2
#define KEY4_DOWN_CLEAR_GPIO_PIN      GPIO_PIN_5
#define KEY5_SETTING_GPIO_PIN         GPIO_PIN_6
#define KEY6_UP_GPIO_PIN              GPIO_PIN_7

/* 信驰达蓝牙模块引脚 */
#define BT_EN_GPIO_PORT               GPIOB
#define BT_EN_GPIO_PIN                GPIO_PIN_0   /* EN: 低电平有效，拉低使能广播/连接 */
#define BT_BRTS_GPIO_PORT             GPIOB
#define BT_BRTS_GPIO_PIN              GPIO_PIN_1    /* BRTS: 主机发送请求，低电平有效 */

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  /* 释放 JTAG 占用的部分引脚，保留 SWD 下载调试功能。 */
  __HAL_AFIO_REMAP_SWJ_NOJTAG();

  HAL_GPIO_WritePin(BUZZER_GPIO_PORT, BUZZER_GPIO_PIN, BUZZER_OFF_LEVEL);
  HAL_GPIO_WritePin(LAMP_ENABLE_GPIO_PORT, LAMP_ENABLE_GPIO_PIN, LAMP_ENABLE_OFF_LEVEL);

  GPIO_InitStruct.Pin = BUZZER_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BUZZER_GPIO_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = LAMP_ENABLE_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  HAL_GPIO_Init(LAMP_ENABLE_GPIO_PORT, &GPIO_InitStruct);

  /* KY-032 低电平有效，PA12 使用上拉输入+双边沿外部中断。 */
  GPIO_InitStruct.Pin = PIR_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(PIR_GPIO_PORT, &GPIO_InitStruct);

  /* 板上物理按键3位于 PC2，物理按键4/5/6位于 PB5/PB6/PB7。 */
  GPIO_InitStruct.Pin = KEY3_UNUSED_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(KEY3_UNUSED_GPIO_PORT, &GPIO_InitStruct);

  GPIO_InitStruct.Pin = KEY4_DOWN_CLEAR_GPIO_PIN |
                        KEY5_SETTING_GPIO_PIN |
                        KEY6_UP_GPIO_PIN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  HAL_NVIC_SetPriority(EXTI2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI2_IRQn);
  HAL_NVIC_SetPriority(EXTI9_5_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* 信驰达蓝牙模块: PB0=EN(低电平有效), PB1=BRTS(低电平有效) */
  HAL_GPIO_WritePin(BT_EN_GPIO_PORT, BT_EN_GPIO_PIN, GPIO_PIN_RESET);   /* EN拉低: 使能广播 */
  HAL_GPIO_WritePin(BT_BRTS_GPIO_PORT, BT_BRTS_GPIO_PIN, GPIO_PIN_SET); /* BRTS拉高: 默认无数据 */

  GPIO_InitStruct.Pin = BT_EN_GPIO_PIN | BT_BRTS_GPIO_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
