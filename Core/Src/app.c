#include "app.h"
#include "peripherals.h"
#include "eeprom.h"
#include "oled.h"
#include "buzzer.h"
#include "bluetooth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* 硬件通道和默认参数集中放在文件开头，方便新手查看和修改。 */
#define LIGHT_ADC_CHANNEL             ADC_CHANNEL_15
#define TEMP_ADC_CHANNEL              ADC_CHANNEL_TEMPSENSOR
#define LIGHT_PWM_CHANNEL             TIM_CHANNEL_3
#define PWM_PERIOD_COUNT              2000U

#define PIR_MAIN_PORT                 GPIOA
#define PIR_MAIN_PIN                  GPIO_PIN_12
#define PIR_ACTIVE_LEVEL              GPIO_PIN_RESET

#define KEY4_PIN                      GPIO_PIN_5
#define KEY5_PIN                      GPIO_PIN_6
#define KEY6_PIN                      GPIO_PIN_7

#define LAMP_PORT                     GPIOD
#define LAMP_PIN                      GPIO_PIN_14

#define DEFAULT_LIGHT_THRESHOLD       3000U
#define DEFAULT_DELAY_TIME            40U
#define DEFAULT_BRIGHTNESS            70U
#define OVERHEAT_TEMP                 350U      /* 35.0 摄氏度报警 */
#define OVERHEAT_RECOVER_TEMP         270U      /* 27.0 摄氏度恢复 */

#define LIGHT_STEP                    100U
#define DELAY_STEP                    10U
#define BRIGHT_STEP                   10U
#define MAX_DELAY_TIME                3600U
#define EEPROM_VALID_MARK             0x5AA55AA5UL

#define ADC_FULL_SCALE                4095UL
#define ADC_REFERENCE_MV              3300UL
#define TEMP_V25_MV                   1430L
#define TEMP_SLOPE_X10                43L
#define TEMP_CALIBRATION              (-150L)

#define ADC_PERIOD_TICK               1U        /* TIM2 200ms * 1 = 200ms */
#define OLED_PERIOD_TICK              3U        /* TIM2 200ms * 3 = 600ms */
#define ONE_SECOND_TICK               5U        /* TIM2 200ms * 5 = 1s */
#define EEPROM_RECORD_COUNT           10U

/* 系统运行数据。变量名尽量直接对应功能，方便汇报和调试。 */
static uint16_t light_value;
static uint16_t light_raw_value;
static uint16_t temperature_adc_value;
static uint16_t temperature_value;             /* 单位 0.1 摄氏度，例如 253 表示 25.3 度 */

static uint16_t light_threshold;
static uint16_t delay_time;
static uint16_t remaining_time;
static uint8_t default_brightness;
static uint8_t current_brightness;
static uint8_t target_brightness;
static uint32_t lighting_count;

static uint8_t pir_present;
static uint8_t pir_raw_level;
static uint8_t pir_idle_raw_level;
static uint8_t pir_idle_ready;
static uint8_t manual_light_on;
static uint8_t manual_off_lock;
static uint8_t oled_page;
static uint8_t setting_item;                   /* 0=无，1=光线阈值，2=延时，3=亮度 */
static uint8_t bt_realtime_enable;
static uint8_t eeprom_record_index;
static uint8_t eeprom_record_count;
static uint32_t last_key_tick;
static uint32_t last_fade_tick;
static SystemState_t system_state;

static volatile uint8_t adc_flag;
static volatile uint8_t oled_flag;
static volatile uint8_t second_flag;
static volatile uint8_t pir_check_flag;
static volatile uint8_t pir_event_flag;
static volatile uint16_t key_event_pin;

/* 串口1打印调试信息。 */
static void Debug_Print(const char *format, ...)
{
  char text[160];
  va_list args;
  int len;

  va_start(args, format);
  len = vsnprintf(text, sizeof(text), format, args);
  va_end(args);

  if (len <= 0) return;
  if (len >= (int)sizeof(text)) len = sizeof(text) - 1;
  HAL_UART_Transmit(&huart1, (uint8_t *)text, (uint16_t)len, 200U);
}

/* 读取一个 ADC 通道。主循环调用，不在中断里采样。 */
static uint16_t ADC_Read(uint32_t channel)
{
  ADC_ChannelConfTypeDef config = {0};
  uint16_t value = 0;

  config.Channel = channel;
  config.Rank = ADC_REGULAR_RANK_1;
  config.SamplingTime = (channel == TEMP_ADC_CHANNEL) ?
                        ADC_SAMPLETIME_239CYCLES_5 :
                        ADC_SAMPLETIME_55CYCLES_5;

  if (HAL_ADC_ConfigChannel(&hadc1, &config) != HAL_OK) return 0;
  HAL_ADC_Start(&hadc1);
  if (HAL_ADC_PollForConversion(&hadc1, 10U) == HAL_OK)
    value = (uint16_t)HAL_ADC_GetValue(&hadc1);
  HAL_ADC_Stop(&hadc1);

  return value;
}

/* 光线检测：转换后数值越大表示外界越亮。 */
static void Light_Read(void)
{
  light_raw_value = ADC_Read(LIGHT_ADC_CHANNEL);
  light_value = (uint16_t)(ADC_FULL_SCALE - light_raw_value);
}

/* 判断暗光。只有暗光和红外同时满足，才允许自动开灯。 */
static uint8_t Light_Check(void)
{
  return (light_value < light_threshold);
}

/* 温度检测：采样 8 次取平均，降低抖动。 */
static void Temp_Read(void)
{
  uint8_t i;
  uint32_t sum = 0;
  uint32_t voltage_mv;
  int32_t temp;

  (void)ADC_Read(TEMP_ADC_CHANNEL);
  for (i = 0; i < 8U; i++)
    sum += ADC_Read(TEMP_ADC_CHANNEL);

  temperature_adc_value = (uint16_t)(sum / 8U);
  voltage_mv = (uint32_t)temperature_adc_value * ADC_REFERENCE_MV / ADC_FULL_SCALE;

  temp = 250L + (TEMP_V25_MV - (int32_t)voltage_mv) * 100L / TEMP_SLOPE_X10
         + TEMP_CALIBRATION;
  if (temp < 0L) temp = 0L;
  if (temp > 1500L) temp = 1500L;
  temperature_value = (uint16_t)temp;
}

/* 读取红外输入原始电平：项目只使用 PA12 作为红外输入。 */
static void PIR_ReadRawLevel(void)
{
  pir_raw_level = (HAL_GPIO_ReadPin(PIR_MAIN_PORT, PIR_MAIN_PIN) == GPIO_PIN_SET) ? 1U : 0U;
}

/* 上电记录红外模块的空闲电平。
 * KY-032 调节电位器后，空闲电平可能不同，所以这里先记住“无人时”的电平。
 */
static void PIR_CalibrateIdleLevel(void)
{
  PIR_ReadRawLevel();
  pir_idle_raw_level = pir_raw_level;
  pir_idle_ready = 1U;
}

/* 红外检测：优先使用空闲电平比较，电平变化就认为检测到人体靠近。 */
static uint8_t PIR_Detect(void)
{
  PIR_ReadRawLevel();

  if (pir_idle_ready)
  {
    if (pir_raw_level != pir_idle_raw_level) return 1U;
    return 0U;
  }

  if (HAL_GPIO_ReadPin(PIR_MAIN_PORT, PIR_MAIN_PIN) == PIR_ACTIVE_LEVEL) return 1U;
  return 0U;
}

/* 设置 PWM 亮度，同时控制灯光使能。 */
static void PWM_SetBrightness(uint8_t brightness)
{
  uint32_t compare;

  if (brightness > 100U) brightness = 100U;
  compare = (uint32_t)brightness * PWM_PERIOD_COUNT / 100U;

  __HAL_TIM_SET_COMPARE(&htim2, LIGHT_PWM_CHANNEL, compare);
  current_brightness = brightness;

  HAL_GPIO_WritePin(LAMP_PORT, LAMP_PIN,
                    brightness > 0U ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* 简单渐变：每 20ms 向目标亮度靠近 1%。 */
static void PWM_FadeProcess(void)
{
  if (HAL_GetTick() - last_fade_tick < 20U) return;
  last_fade_tick = HAL_GetTick();

  if (current_brightness < target_brightness)
    PWM_SetBrightness(current_brightness + 1U);
  else if (current_brightness > target_brightness)
    PWM_SetBrightness(current_brightness - 1U);
}

/* 保存当前设置和亮灯次数。 */
static uint8_t EEPROM_SaveConfig(void)
{
  EepromData_t data = {0};

  data.valid_mark = EEPROM_VALID_MARK;
  data.light_threshold = light_threshold;
  data.delay_time = delay_time;
  data.default_brightness = default_brightness;
  data.lighting_count = lighting_count;

  if (EEPROM_SaveData(&data) == HAL_OK)
  {
    Debug_Print("[EEPROM] save OK\r\n");
    return 1U;
  }

  Debug_Print("[EEPROM] save ERROR\r\n");
  return 0U;
}

/* 上电读取 EEPROM；无效时使用默认值。 */
static void EEPROM_LoadConfig(void)
{
  EepromData_t data = {0};

  if (EEPROM_ReadData(&data) == HAL_OK && EEPROM_IsDataValid(&data))
  {
    light_threshold = data.light_threshold;
    delay_time = data.delay_time;
    default_brightness = data.default_brightness;
    lighting_count = data.lighting_count;
    Debug_Print("[EEPROM] read OK\r\n");
    return;
  }

  light_threshold = DEFAULT_LIGHT_THRESHOLD;
  delay_time = DEFAULT_DELAY_TIME;
  default_brightness = DEFAULT_BRIGHTNESS;
  lighting_count = 0;
  Debug_Print("[EEPROM] use defaults\r\n");
  (void)EEPROM_SaveConfig();
}

/* 自动或手动开灯。auto_start=1 表示红外自动触发，需要累计次数。 */
static void Light_On(uint8_t auto_start)
{
  if (system_state == SYS_OVERHEAT) return;

  system_state = SYS_LIGHTING;
  target_brightness = default_brightness;
  remaining_time = delay_time;

  if (auto_start)
  {
    lighting_count++;
    (void)EEPROM_SaveConfig();
    Buzzer_Beep(1);
  }

  Debug_Print("[LIGHT] ON bright=%u%%\r\n", default_brightness);
}

/* 关灯。delay_done=1 表示延时结束关灯，需要短响两次。 */
static void Light_Off(uint8_t delay_done)
{
  target_brightness = 0;
  manual_light_on = 0;
  remaining_time = 0;

  if (system_state != SYS_OVERHEAT)
    system_state = SYS_STANDBY;

  if (delay_done) Buzzer_Beep(2);
  Debug_Print("[LIGHT] OFF\r\n");
}

/* 过热保护：温度过高时强制关灯，恢复后回到待机。 */
static void Temp_Check(void)
{
  if (system_state != SYS_OVERHEAT && temperature_value >= OVERHEAT_TEMP)
  {
    system_state = SYS_OVERHEAT;
    target_brightness = 0;
    remaining_time = 0;
    manual_light_on = 0;
    manual_off_lock = 0;
    PWM_SetBrightness(0);
    Buzzer_SetOverheat(1);
    Debug_Print("[TEMP] OVERHEAT %.1fC\r\n", temperature_value / 10.0f);
  }
  else if (system_state == SYS_OVERHEAT && temperature_value <= OVERHEAT_RECOVER_TEMP)
  {
    system_state = SYS_STANDBY;
    Buzzer_SetOverheat(0);
    Debug_Print("[TEMP] recover\r\n");
  }
}

static const char *State_TextFromValue(uint8_t state)
{
  if (state == SYS_LIGHTING) return "LIGHT";
  if (state == SYS_OVERHEAT) return "HOT";
  return "IDLE";
}

static const char *State_Text(void)
{
  return State_TextFromValue((uint8_t)system_state);
}

/* 蓝牙 GET 和自动上报都使用这个函数生成状态文本。 */
static void Build_CurrentStatusText(char *text, uint16_t size)
{
  snprintf(text, size,
           "[SYS] L=%u T=%.1fC B=%u%% %s Time=%u PIR=%u\r\n",
           light_value,
           temperature_value / 10.0f,
           current_brightness,
           State_Text(),
           remaining_time,
           pir_present);
}

static void Bluetooth_SendStatus(void)
{
  char text[120];

  /* 手机端和 IPOP/电脑串口使用同一格式，方便直接对照。 */
  Build_CurrentStatusText(text, sizeof(text));
  Bluetooth_Send(text);
}
/* 每秒把当前 I/O/P 状态保存到 EEPROM，最多保留最近 10 条。 */
static void EEPROM_SaveLatestRecord(const char *status_text)
{
  EepromRecord_t record;
  uint8_t i;

  record.valid_mark = 0U;
  for (i = 0; i < sizeof(record.text); i++) record.text[i] = '\0';
  strncpy(record.text, status_text, sizeof(record.text) - 1U);
  record.checksum = 0U;

  (void)EEPROM_SaveRecord(eeprom_record_index, &record);

  eeprom_record_index++;
  if (eeprom_record_index >= EEPROM_RECORD_COUNT) eeprom_record_index = 0U;
  if (eeprom_record_count < EEPROM_RECORD_COUNT) eeprom_record_count++;
}

/* GET E 调用：停止实时上报后，把 EEPROM 中保存的 10 条记录发给手机端。 */
static void Bluetooth_SendEepromRecords(void)
{
  EepromRecord_t record;
  char text[96];
  uint8_t i;
  uint8_t index;

  Bluetooth_Send("E,BEGIN\r\n");

  for (i = 0; i < EEPROM_RECORD_COUNT; i++)
  {
    index = (uint8_t)((eeprom_record_index + i) % EEPROM_RECORD_COUNT);

    if (EEPROM_ReadRecord(index, &record) == HAL_OK && EEPROM_IsRecordValid(&record))
    {
      snprintf(text, sizeof(text), "E%u,%s", i + 1U, record.text);
    }
    else
    {
      snprintf(text, sizeof(text), "E%u,EMPTY\r\n", i + 1U);
    }

    Bluetooth_Send(text);
  }

  Bluetooth_Send("E,END\r\n");
}

/* 红外逻辑：暗光 + 有人 -> 自动开灯；人离开后开始倒计时。 */
static void PIR_Process(void)
{
  if (!pir_event_flag) return;
  pir_event_flag = 0;

  if (system_state == SYS_OVERHEAT) return;

  if (!pir_present)
  {
    manual_off_lock = 0;
    Debug_Print("[PIR] leave\r\n");
    return;
  }

  if (system_state == SYS_LIGHTING && !manual_light_on)
  {
    Debug_Print("[PIR] keep lighting\r\n");
    return;
  }

  if (system_state == SYS_STANDBY && !manual_off_lock)
  {
    Light_Read();                         /* 触发瞬间重新读取光线 */
    if (Light_Check())
      Light_On(1);
    else
      Debug_Print("[PIR] bright block light=%u thr=%u\r\n", light_value, light_threshold);
  }
}

/* 按键5切换页面/选项，按键6增加，按键4减少或清零。 */
static void Key_Process(void)
{
  uint16_t pin;
  int8_t dir = 0;
  uint16_t old_light = light_threshold;
  uint16_t old_delay = delay_time;
  uint8_t old_bright = default_brightness;
  uint32_t old_count = lighting_count;

  if (key_event_pin == 0U) return;
  pin = key_event_pin;
  key_event_pin = 0U;

  if (HAL_GetTick() - last_key_tick < 150U) return;
  last_key_tick = HAL_GetTick();

  if (pin == KEY5_PIN)
  {
    if (oled_page == 0U)
    {
      oled_page = 1U;
      setting_item = 1U;
    }
    else if (oled_page == 1U && setting_item < 3U)
    {
      setting_item++;
    }
    else if (oled_page == 1U)
    {
      oled_page = 2U;
      setting_item = 0U;
    }
    else
    {
      oled_page = 0U;
      setting_item = 0U;
    }
  }
  else if (pin == KEY6_PIN && oled_page == 1U)
  {
    dir = 1;
  }
  else if (pin == KEY4_PIN && oled_page == 1U)
  {
    dir = -1;
  }
  else if (pin == KEY4_PIN && oled_page == 2U)
  {
    lighting_count = 0;
    if (EEPROM_SaveConfig())
    {
      Buzzer_Beep(3);
      Debug_Print("[KEY] count clear\r\n");
    }
    else
      lighting_count = old_count;
  }

  if (dir != 0)
  {
    if (setting_item == 1U)
    {
      if (dir > 0 && light_threshold + LIGHT_STEP <= ADC_FULL_SCALE) light_threshold += LIGHT_STEP;
      if (dir < 0 && light_threshold >= LIGHT_STEP) light_threshold -= LIGHT_STEP;
    }
    else if (setting_item == 2U)
    {
      if (dir > 0 && delay_time + DELAY_STEP <= MAX_DELAY_TIME) delay_time += DELAY_STEP;
      if (dir < 0 && delay_time > DELAY_STEP) delay_time -= DELAY_STEP;
    }
    else if (setting_item == 3U)
    {
      if (dir > 0 && default_brightness + BRIGHT_STEP <= 100U) default_brightness += BRIGHT_STEP;
      if (dir < 0 && default_brightness >= BRIGHT_STEP) default_brightness -= BRIGHT_STEP;
      if (system_state == SYS_LIGHTING) target_brightness = default_brightness;
    }

    if (EEPROM_SaveConfig())
    {
      Buzzer_Beep(3);
      Debug_Print("[KEY] config light=%u delay=%u bright=%u\r\n",
                  light_threshold, delay_time, default_brightness);
    }
    else
    {
      light_threshold = old_light;
      delay_time = old_delay;
      default_brightness = old_bright;
    }
  }

  oled_flag = 1;
}

/* 每秒处理倒计时、串口日志和蓝牙实时上报。 */
static void Time_Process(void)
{
  static uint8_t debug_count = 0;

  if (!second_flag) return;
  second_flag = 0;

  if (system_state == SYS_LIGHTING && !manual_light_on && remaining_time > 0U)
  {
    remaining_time--;
    if (remaining_time == 0U) Light_Off(1);
  }

  debug_count++;
  if (debug_count >= 3U)
  {
    char status_text[120];
    debug_count = 0;

    /* IPOP 输出一条，就把同一条原样发给手机并保存到 EEPROM。 */
    Build_CurrentStatusText(status_text, sizeof(status_text));
    Debug_Print("%s", status_text);
    EEPROM_SaveLatestRecord(status_text);
    if (bt_realtime_enable)
      Bluetooth_Send(status_text);
  }
}

/* OLED 实时状态页。 */
static void OLED_ShowStatus(void)
{
  char text[24];

  OLED_ShowChinese(0, 0, OLED_HZ_GUANG); OLED_ShowChinese(16, 0, OLED_HZ_XIAN);
  snprintf(text, sizeof(text), ":%u", light_value); OLED_ShowString16(36, 0, text);

  OLED_ShowChinese(0, 2, OLED_HZ_WEN); OLED_ShowChinese(16, 2, OLED_HZ_DU);
  snprintf(text, sizeof(text), ":%u.%u", temperature_value / 10U, temperature_value % 10U);
  OLED_ShowString16(36, 2, text);

  OLED_ShowChinese(0, 4, OLED_HZ_LIANG); OLED_ShowChinese(16, 4, OLED_HZ_DU);
  snprintf(text, sizeof(text), ":%u%%", current_brightness); OLED_ShowString16(36, 4, text);

  OLED_ShowChinese(0, 6, OLED_HZ_ZHUANG); OLED_ShowChinese(16, 6, OLED_HZ_TAI);
  OLED_ShowString16(34, 6, ":");
  if (system_state == SYS_LIGHTING)
  {
    OLED_ShowChinese(44, 6, OLED_HZ_ZHAO); OLED_ShowChinese(60, 6, OLED_HZ_MING);
  }
  else if (system_state == SYS_OVERHEAT)
  {
    OLED_ShowChinese(44, 6, OLED_HZ_GUO); OLED_ShowChinese(60, 6, OLED_HZ_RE);
  }
  else
  {
    OLED_ShowChinese(44, 6, OLED_HZ_DAI); OLED_ShowChinese(60, 6, OLED_HZ_JI);
  }
}

/* OLED 参数设置页。 */
static void OLED_ShowSetting(void)
{
  char text[24];

  OLED_ShowChinese(0, 0, OLED_HZ_CAN); OLED_ShowChinese(16, 0, OLED_HZ_SHU);
  OLED_ShowChinese(32, 0, OLED_HZ_SHE); OLED_ShowChinese(48, 0, OLED_HZ_ZHI);

  OLED_ShowString16(0, 2, setting_item == 1U ? ">" : " ");
  OLED_ShowChinese(8, 2, OLED_HZ_GUANG); OLED_ShowChinese(24, 2, OLED_HZ_XIAN);
  snprintf(text, sizeof(text), ":%u", light_threshold); OLED_ShowString16(42, 2, text);

  OLED_ShowString16(0, 4, setting_item == 2U ? ">" : " ");
  OLED_ShowChinese(8, 4, OLED_HZ_YAN); OLED_ShowChinese(24, 4, OLED_HZ_SHI);
  snprintf(text, sizeof(text), ":%u", delay_time); OLED_ShowString16(42, 4, text);
  OLED_ShowChinese(86, 4, OLED_HZ_MIAO);

  OLED_ShowString16(0, 6, setting_item == 3U ? ">" : " ");
  OLED_ShowChinese(8, 6, OLED_HZ_LIANG); OLED_ShowChinese(24, 6, OLED_HZ_DU);
  snprintf(text, sizeof(text), ":%u%%", default_brightness); OLED_ShowString16(42, 6, text);
}

/* OLED 统计页。 */
static void OLED_ShowCount(void)
{
  char text[24];

  OLED_ShowChinese(0, 0, OLED_HZ_GAN); OLED_ShowChinese(16, 0, OLED_HZ_YING);
  OLED_ShowChinese(32, 0, OLED_HZ_TONG); OLED_ShowChinese(48, 0, OLED_HZ_JI2);

  OLED_ShowChinese(0, 2, OLED_HZ_LIANG); OLED_ShowChinese(16, 2, OLED_HZ_DENG);
  OLED_ShowChinese(32, 2, OLED_HZ_CI); OLED_ShowChinese(48, 2, OLED_HZ_SHU);
  snprintf(text, sizeof(text), "%lu", (unsigned long)lighting_count);
  OLED_ShowString16(70, 2, text);

  OLED_ShowChinese(0, 4, OLED_HZ_SHENG); OLED_ShowChinese(16, 4, OLED_HZ_YU);
  OLED_ShowChinese(32, 4, OLED_HZ_MIAO); OLED_ShowChinese(48, 4, OLED_HZ_SHU);
  snprintf(text, sizeof(text), "%u", remaining_time); OLED_ShowString16(70, 4, text);

  OLED_ShowChinese(0, 6, OLED_HZ_AN); OLED_ShowChinese(16, 6, OLED_HZ_JIAN);
  OLED_ShowString16(34, 6, "4");
  OLED_ShowChinese(44, 6, OLED_HZ_QING); OLED_ShowChinese(60, 6, OLED_HZ_LING);
}

/* 根据页码刷新 OLED。 */
static void OLED_ShowPage(void)
{
  OLED_Clear();
  if (oled_page == 0U) OLED_ShowStatus();
  else if (oled_page == 1U) OLED_ShowSetting();
  else OLED_ShowCount();
  OLED_Refresh();
}

/* 规范化蓝牙命令：去掉首尾空白，转大写，遇到 # 结束。 */
static void Command_Normalize(const char *input, char *output, uint16_t size)
{
  uint16_t i = 0;
  char ch;

  if (size == 0U) return;
  if (input == NULL)
  {
    output[0] = '\0';
    return;
  }

  while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') input++;

  while (*input != '\0' && i < size - 1U)
  {
    ch = *input++;
    if (ch == '#') break;
    if (ch == '\r' || ch == '\n') break;
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    output[i++] = ch;
  }

  while (i > 0U && (output[i - 1U] == ' ' || output[i - 1U] == '\t')) i--;
  output[i] = '\0';
}

/* 解析 SET 命令后面的数字。 */
static uint8_t Command_GetValue(const char *command, const char *prefix, long *value)
{
  char *end;
  uint16_t prefix_len;

  prefix_len = (uint16_t)strlen(prefix);

  /* 支持 SET DELAY=20、SET DELAY 20、SET DELAY:20。 */
  if (strncmp(command, prefix, prefix_len) != 0) return 0U;
  command += prefix_len;

  while (*command == ' ' || *command == '\t' || *command == '=' || *command == ':')
    command++;

  if (*command == '\0') return 0U;

  *value = strtol(command, &end, 10);
  if (end == command) return 0U;

  /* 手机蓝牙有时会在数字后附带乱码或 #，数字已取到就认为命令有效。 */
  return 1U;
}

/* 从命令任意位置提取第一个数字，专门处理手机蓝牙带乱码的情况。 */
static uint8_t Command_FindFirstNumber(const char *command, long *value)
{
  char *end;

  while (*command != '\0')
  {
    if ((*command >= '0' && *command <= '9') || *command == '-')
    {
      *value = strtol(command, &end, 10);
      return (end != command) ? 1U : 0U;
    }
    command++;
  }
  return 0U;
}
/* 初始化应用层。 */
void App_Init(void)
{
  system_state = SYS_STANDBY;
  oled_page = 0;
  setting_item = 0;
  current_brightness = 0;
  target_brightness = 0;
  manual_light_on = 0;
  manual_off_lock = 0;
  pir_present = 0;
  pir_idle_ready = 0;

  Buzzer_Init();
  EEPROM_Init();
  EEPROM_LoadConfig();
  OLED_Init();
  HAL_TIM_PWM_Start(&htim2, LIGHT_PWM_CHANNEL);
  HAL_TIM_Base_Start_IT(&htim2);
  Bluetooth_Init();

  Light_Read();
  Temp_Read();
  PIR_CalibrateIdleLevel();
  Temp_Check();
  OLED_ShowPage();

  Debug_Print("[INIT] PIR idle: P12=%u\r\n", pir_idle_raw_level);
  Debug_Print("[INIT] Smart Light started\r\n");
}

/* 主循环：所有耗时逻辑都在这里执行。 */
void App_Loop(void)
{
  if (pir_check_flag)
  {
    pir_check_flag = 0;
    adc_flag = 1U;
  }

  if (adc_flag)
  {
    static uint8_t pir_debounce_value = 0U;
    static uint8_t pir_debounce_count = 0U;
    uint8_t now_pir;

    adc_flag = 0;
    Light_Read();
    Temp_Read();
    Temp_Check();

    now_pir = PIR_Detect();
    if (now_pir == pir_debounce_value)
    {
      if (pir_debounce_count < 3U) pir_debounce_count++;
      if (pir_debounce_count >= 3U && pir_present != now_pir)
      {
        pir_present = now_pir;
        pir_event_flag = 1U;
      }
    }
    else
    {
      pir_debounce_value = now_pir;
      pir_debounce_count = 1U;
    }

    if (system_state == SYS_STANDBY && pir_present && !manual_off_lock && Light_Check())
      pir_event_flag = 1U;

    oled_flag = 1;
  }

  Bluetooth_Process();
  PIR_Process();
  Key_Process();
  Time_Process();
  PWM_FadeProcess();
  Buzzer_Process();

  if (oled_flag)
  {
    oled_flag = 0;
    OLED_ShowPage();
  }
}

/* 外部中断：只记录红外或按键事件。 */
void App_OnGpioInterrupt(uint16_t gpio_pin)
{
  if (gpio_pin == PIR_MAIN_PIN)
    pir_check_flag = 1U;
  else
    key_event_pin = gpio_pin;
}

/* TIM2 每 200ms 进入一次中断，这里只设置标志位。 */
void App_OnTimerInterrupt(TIM_HandleTypeDef *htim)
{
  static uint16_t tick = 0;

  if (htim->Instance != TIM2) return;

  tick++;
  if ((tick % ADC_PERIOD_TICK) == 0U) adc_flag = 1U;
  if ((tick % OLED_PERIOD_TICK) == 0U) oled_flag = 1U;
  if (tick >= ONE_SECOND_TICK)
  {
    tick = 0;
    second_flag = 1U;
  }
}

/* 蓝牙命令处理。 */
void App_HandleBluetoothCommand(const char *command)
{
  char cmd[64];
  long value;
  uint16_t old_light = light_threshold;
  uint16_t old_delay = delay_time;
  uint8_t old_bright = default_brightness;
  uint32_t old_count = lighting_count;

  Command_Normalize(command, cmd, sizeof(cmd));
  Debug_Print("[BLE] RX: %s\r\n", cmd);

  if (strncmp(cmd, "TTM:", 4U) == 0) return;       /* 蓝牙模块自己的连接提示，忽略 */

  if (strcmp(cmd, "GET D") == 0)
  {
    bt_realtime_enable = 1U;
    Bluetooth_Send("OK,D=ON\r\n");
    return;
  }

  if (strcmp(cmd, "GET E") == 0)
  {
    bt_realtime_enable = 0U;
    Bluetooth_Send("OK,D=OFF\r\n");
    Bluetooth_SendEepromRecords();
    return;
  }

  if (strcmp(cmd, "GET") == 0)
  {
    Bluetooth_SendStatus();
    return;
  }

  if (strcmp(cmd, "ON") == 0)
  {
    if (system_state == SYS_OVERHEAT)
      Bluetooth_Send("ERROR:OVERHEAT\r\n");
    else
    {
      manual_light_on = 1U;
      manual_off_lock = 0U;
      Light_On(0);
      Bluetooth_Send("OK\r\n");
    }
    return;
  }

  if (strcmp(cmd, "OFF") == 0)
  {
    Light_Off(0);
    manual_off_lock = pir_present;
    Bluetooth_Send("OK\r\n");
    return;
  }

  if (strcmp(cmd, "CLEAR") == 0 || strcmp(cmd, "CLEAN") == 0)
  {
    lighting_count = 0;
    if (EEPROM_SaveConfig())
    {
      Buzzer_Beep(3);
      Debug_Print("[BLE] count clear\r\n");
      Bluetooth_Send("OK,C=0\r\n");
    }
    else
    {
      lighting_count = old_count;
      Bluetooth_Send("ERROR:EEPROM\r\n");
    }
    return;
  }

  if (Command_GetValue(cmd, "SET LIGHT", &value))
  {
    if (value < 0 || value > (long)ADC_FULL_SCALE)
    {
      Bluetooth_Send("ERROR:RANGE 0-4095\r\n");
      return;
    }
    light_threshold = (uint16_t)value;
  }
  else if (strncmp(cmd, "SET DELAY", 9U) == 0)
  {
    if (!Command_FindFirstNumber(cmd + 9U, &value))
    {
      Bluetooth_Send("ERROR:NO DELAY\r\n");
      return;
    }
    if (value <= 0 || value > MAX_DELAY_TIME)
    {
      Bluetooth_Send("ERROR:RANGE 1-3600\r\n");
      return;
    }
    delay_time = (uint16_t)value;
    if (system_state == SYS_LIGHTING && !manual_light_on)
      remaining_time = delay_time;
    Debug_Print("[BLE] delay set=%u\r\n", delay_time);
  }
  else if (Command_GetValue(cmd, "SET BRIGHT", &value))
  {
    if (value < 0 || value > 100)
    {
      Bluetooth_Send("ERROR:RANGE 0-100\r\n");
      return;
    }
    default_brightness = (uint8_t)value;
    if (system_state == SYS_LIGHTING) target_brightness = default_brightness;
  }
  else
  {
    Bluetooth_Send("ERROR:UNKNOWN COMMAND\r\n");
    return;
  }

  if (EEPROM_SaveConfig())
  {
    Buzzer_Beep(3);
    Debug_Print("[BLE] config light=%u delay=%u bright=%u\r\n",
                light_threshold, delay_time, default_brightness);
    Bluetooth_Send("OK\r\n");
  }
  else
  {
    light_threshold = old_light;
    delay_time = old_delay;
    default_brightness = old_bright;
    Bluetooth_Send("ERROR:EEPROM\r\n");
  }
}
