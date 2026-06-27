#include "buzzer.h"

/* 实板蜂鸣器使用 PC6/TIM3_CH1 全重映射，输出约 2.5kHz 方波。 */
#define BUZZER_GPIO_PORT       GPIOC
#define BUZZER_GPIO_PIN        GPIO_PIN_6
#define BUZZER_PWM_CHANNEL     TIM_CHANNEL_1
#define BUZZER_PWM_PERIOD      399U
#define BUZZER_PWM_COMPARE     200U

static uint8_t beep_times_left;
static uint8_t overheat_alarm;
static uint8_t tone_enabled;
static uint32_t next_pattern_time;
static TIM_HandleTypeDef htim3_buzzer;

/* 打开或关闭 TIM3_CH1 的 2.5kHz 方波，不在主循环中快速翻转 GPIO。 */
static void Buzzer_EnableTone(uint8_t enabled)
{
  tone_enabled = enabled;
  __HAL_TIM_SET_COMPARE(&htim3_buzzer, BUZZER_PWM_CHANNEL,
                        enabled ? BUZZER_PWM_COMPARE : 0U);
}

/* 初始化 PC6/TIM3_CH1 蜂鸣器 PWM，主程序上电时调用一次。 */
void Buzzer_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  TIM_OC_InitTypeDef pwm = {0};

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_AFIO_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();
  __HAL_AFIO_REMAP_TIM3_ENABLE();

  gpio.Pin = BUZZER_GPIO_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(BUZZER_GPIO_PORT, &gpio);

  htim3_buzzer.Instance = TIM3;
  htim3_buzzer.Init.Prescaler = 72U - 1U;
  htim3_buzzer.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3_buzzer.Init.Period = BUZZER_PWM_PERIOD;
  htim3_buzzer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3_buzzer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim3_buzzer) != HAL_OK) Error_Handler();

  pwm.OCMode = TIM_OCMODE_PWM1;
  pwm.Pulse = 0U;
  pwm.OCPolarity = TIM_OCPOLARITY_HIGH;
  pwm.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3_buzzer, &pwm, BUZZER_PWM_CHANNEL) != HAL_OK)
    Error_Handler();
  if (HAL_TIM_PWM_Start(&htim3_buzzer, BUZZER_PWM_CHANNEL) != HAL_OK)
    Error_Handler();

  beep_times_left = 0;
  overheat_alarm = 0;
  Buzzer_EnableTone(0);
}

/* 请求短响若干次。函数不延时，实际节奏由 Buzzer_Process 完成。 */
void Buzzer_Beep(uint8_t times)
{
  if (overheat_alarm || times == 0U) return;
  beep_times_left = times;
  Buzzer_EnableTone(1);
  next_pattern_time = HAL_GetTick() + 200U;
}

/* 过热时持续鸣响，温度恢复后立即关闭。 */
void Buzzer_SetOverheat(uint8_t enabled)
{
  overheat_alarm = enabled;
  beep_times_left = 0;
  Buzzer_EnableTone(enabled);
}

/* 主循环反复调用，只负责完成 1/2/3 声节奏。 */
void Buzzer_Process(void)
{
  uint32_t now = HAL_GetTick();

  /* 过热保护要求持续报警，每次循环都保持 PC6 输出音调。 */
  if (overheat_alarm)
  {
    Buzzer_EnableTone(1);
    return;
  }
  if (beep_times_left == 0U) return;
  if ((int32_t)(now - next_pattern_time) < 0) return;

  if (tone_enabled)
  {
    Buzzer_EnableTone(0);
    beep_times_left--;
    next_pattern_time = now + 200U;
  }
  else
  {
    Buzzer_EnableTone(1);
    next_pattern_time = now + 200U;
  }
}
