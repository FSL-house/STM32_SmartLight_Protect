#include "peripherals.h"

/* 外设引脚与通信速率同时记录在 STM32_SmartLight_Protect.ioc。 */
#define LIGHT_ADC_GPIO_PORT       GPIOC
#define LIGHT_ADC_GPIO_PIN        GPIO_PIN_5
#define LIGHT_ADC_CHANNEL         ADC_CHANNEL_15
#define LIGHT_PWM_GPIO_PIN        GPIO_PIN_10
#define LIGHT_PWM_CHANNEL         TIM_CHANNEL_3
#define DEBUG_UART_BAUDRATE       115200U
#define BLUETOOTH_UART_BAUDRATE   115200U

ADC_HandleTypeDef hadc1;
TIM_HandleTypeDef htim2;
UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* 初始化 ADC1。具体通道在 app.c 每次采样前选择。 */
void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef channel = {0};
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_ADC1_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_ADC_CONFIG(RCC_ADCPCLK2_DIV6);

  gpio.Pin = LIGHT_ADC_GPIO_PIN;
  gpio.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(LIGHT_ADC_GPIO_PORT, &gpio);

  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  channel.Channel = LIGHT_ADC_CHANNEL;
  channel.Rank = ADC_REGULAR_RANK_1;
  channel.SamplingTime = ADC_SAMPLETIME_55CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK) Error_Handler();
  HAL_ADCEx_Calibration_Start(&hadc1);
}

/* TIM2 输出 PB10/TIM2_CH3 PWM：周期 200ms，用来让 LED13 可见闪动。 */
void MX_TIM2_Init(void)
{
  TIM_OC_InitTypeDef pwm = {0};
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_TIM2_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();

  /* TIM2 部分重映射2：CH3 输出到 PB10。 */
  __HAL_AFIO_REMAP_TIM2_PARTIAL_2();

  gpio.Pin = LIGHT_PWM_GPIO_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOB, &gpio);

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7200U - 1U;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 2000U - 1U;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) Error_Handler();

  pwm.OCMode = TIM_OCMODE_PWM1;
  pwm.Pulse = 0;
  pwm.OCPolarity = TIM_OCPOLARITY_LOW;
  pwm.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &pwm, LIGHT_PWM_CHANNEL) != HAL_OK) Error_Handler();
  __HAL_TIM_ENABLE_OCxPRELOAD(&htim2, LIGHT_PWM_CHANNEL);

  HAL_NVIC_SetPriority(TIM2_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

/* USART1 用于连接电脑串口助手，打印调试信息。 */
void MX_USART1_UART_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_USART1_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_10;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);

  huart1.Instance = USART1;
  huart1.Init.BaudRate = DEBUG_UART_BAUDRATE;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK) Error_Handler();
}

/* USART2 用于蓝牙模块，使用逐字节接收中断。 */
void MX_USART2_UART_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  gpio.Pin = GPIO_PIN_2;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);
  gpio.Pin = GPIO_PIN_3;
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &gpio);

  huart2.Instance = USART2;
  huart2.Init.BaudRate = BLUETOOTH_UART_BAUDRATE;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK) Error_Handler();
  HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}
