#include "eeprom.h"

#define EEPROM_STORED_SIZE 14U
#define EEPROM_SCL_GPIO_PORT       GPIOE
#define EEPROM_SCL_GPIO_PIN        GPIO_PIN_3
#define EEPROM_SDA_GPIO_PORT       GPIOE
#define EEPROM_SDA_GPIO_PIN        GPIO_PIN_4
#define EEPROM_I2C_WRITE_ADDRESS   0xA0U
#define EEPROM_DATA_ADDRESS        0x00U
#define EEPROM_VALID_MARK          0x5AA55AA5UL
#define MAX_DELAY_TIME             3600U
#define ADC_FULL_SCALE             4095U

/* AT24C128 使用独立的软件 I2C，总线引脚来自配套实验资料。 */
static void EEPROM_I2cDelay(void)
{
  volatile uint8_t i;
  for (i = 0; i < 30U; i++) __NOP();
}

static void EEPROM_Scl(GPIO_PinState state)
{
  HAL_GPIO_WritePin(EEPROM_SCL_GPIO_PORT, EEPROM_SCL_GPIO_PIN, state);
}

static void EEPROM_Sda(GPIO_PinState state)
{
  HAL_GPIO_WritePin(EEPROM_SDA_GPIO_PORT, EEPROM_SDA_GPIO_PIN, state);
}

static void EEPROM_I2cStart(void)
{
  EEPROM_Sda(GPIO_PIN_SET);
  EEPROM_Scl(GPIO_PIN_SET);
  EEPROM_I2cDelay();
  EEPROM_Sda(GPIO_PIN_RESET);
  EEPROM_I2cDelay();
  EEPROM_Scl(GPIO_PIN_RESET);
}

static void EEPROM_I2cStop(void)
{
  EEPROM_Sda(GPIO_PIN_RESET);
  EEPROM_Scl(GPIO_PIN_SET);
  EEPROM_I2cDelay();
  EEPROM_Sda(GPIO_PIN_SET);
  EEPROM_I2cDelay();
}

/* 发送一个字节并检查从机 ACK；返回 1 表示收到应答。 */
static uint8_t EEPROM_I2cWriteByte(uint8_t value)
{
  uint8_t bit;
  GPIO_PinState ack;
  for (bit = 0; bit < 8U; bit++)
  {
    EEPROM_Sda((value & 0x80U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
    EEPROM_I2cDelay();
    EEPROM_Scl(GPIO_PIN_SET);
    EEPROM_I2cDelay();
    EEPROM_Scl(GPIO_PIN_RESET);
    value <<= 1;
  }
  EEPROM_Sda(GPIO_PIN_SET);              /* 开漏输出高电平就是释放 SDA */
  EEPROM_I2cDelay();
  EEPROM_Scl(GPIO_PIN_SET);
  EEPROM_I2cDelay();
  ack = HAL_GPIO_ReadPin(EEPROM_SDA_GPIO_PORT, EEPROM_SDA_GPIO_PIN);
  EEPROM_Scl(GPIO_PIN_RESET);
  return ack == GPIO_PIN_RESET;
}

static uint8_t EEPROM_I2cReadByte(uint8_t send_ack)
{
  uint8_t bit;
  uint8_t value = 0;
  EEPROM_Sda(GPIO_PIN_SET);
  for (bit = 0; bit < 8U; bit++)
  {
    value <<= 1;
    EEPROM_Scl(GPIO_PIN_SET);
    EEPROM_I2cDelay();
    if (HAL_GPIO_ReadPin(EEPROM_SDA_GPIO_PORT, EEPROM_SDA_GPIO_PIN) == GPIO_PIN_SET)
      value |= 1U;
    EEPROM_Scl(GPIO_PIN_RESET);
    EEPROM_I2cDelay();
  }
  EEPROM_Sda(send_ack ? GPIO_PIN_RESET : GPIO_PIN_SET);
  EEPROM_Scl(GPIO_PIN_SET);
  EEPROM_I2cDelay();
  EEPROM_Scl(GPIO_PIN_RESET);
  EEPROM_Sda(GPIO_PIN_SET);
  return value;
}

/* 初始化 AT24C128 的 PE3/PE4 软件 I2C 引脚。 */
void EEPROM_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  __HAL_RCC_GPIOE_CLK_ENABLE();
  gpio.Pin = EEPROM_SCL_GPIO_PIN | EEPROM_SDA_GPIO_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT_OD;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOE, &gpio);
  EEPROM_Scl(GPIO_PIN_SET);
  EEPROM_Sda(GPIO_PIN_SET);
}

/* 计算简单校验和，用来判断 EEPROM 中的数据是否完整。 */
static uint8_t EEPROM_CalculateChecksum(const EepromData_t *data)
{
  uint8_t sum = 0;
  sum += (uint8_t)data->valid_mark;
  sum += (uint8_t)(data->valid_mark >> 8);
  sum += (uint8_t)(data->valid_mark >> 16);
  sum += (uint8_t)(data->valid_mark >> 24);
  sum += (uint8_t)data->light_threshold;
  sum += (uint8_t)(data->light_threshold >> 8);
  sum += (uint8_t)data->delay_time;
  sum += (uint8_t)(data->delay_time >> 8);
  sum += data->default_brightness;
  sum += (uint8_t)data->lighting_count;
  sum += (uint8_t)(data->lighting_count >> 8);
  sum += (uint8_t)(data->lighting_count >> 16);
  sum += (uint8_t)(data->lighting_count >> 24);
  return sum;
}

/* 从 AT24C128 读取参数。24C128 的存储地址必须发送高、低两个字节。 */
HAL_StatusTypeDef EEPROM_ReadData(EepromData_t *data)
{
  uint8_t bytes[EEPROM_STORED_SIZE];
  uint16_t i;
  EEPROM_I2cStart();
  if (!EEPROM_I2cWriteByte(EEPROM_I2C_WRITE_ADDRESS) ||
      !EEPROM_I2cWriteByte((uint8_t)(EEPROM_DATA_ADDRESS >> 8)) ||
      !EEPROM_I2cWriteByte((uint8_t)EEPROM_DATA_ADDRESS))
  {
    EEPROM_I2cStop();
    return HAL_ERROR;
  }
  EEPROM_I2cStart();
  if (!EEPROM_I2cWriteByte(EEPROM_I2C_WRITE_ADDRESS | 1U))
  {
    EEPROM_I2cStop();
    return HAL_ERROR;
  }
  for (i = 0; i < EEPROM_STORED_SIZE; i++)
    bytes[i] = EEPROM_I2cReadByte(i + 1U < EEPROM_STORED_SIZE);
  EEPROM_I2cStop();

  /* 明确按字节还原，避免 C 结构体填充字节影响掉电数据格式。 */
  data->valid_mark = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
                     ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
  data->light_threshold = (uint16_t)bytes[4] | ((uint16_t)bytes[5] << 8);
  data->delay_time = (uint16_t)bytes[6] | ((uint16_t)bytes[7] << 8);
  data->default_brightness = bytes[8];
  data->lighting_count = (uint32_t)bytes[9] | ((uint32_t)bytes[10] << 8) |
                         ((uint32_t)bytes[11] << 16) | ((uint32_t)bytes[12] << 24);
  data->checksum = bytes[13];
  return HAL_OK;
}

/* 逐字节写入，写完等待 5ms，避免 EEPROM 内部写周期尚未结束。 */
HAL_StatusTypeDef EEPROM_SaveData(const EepromData_t *data)
{
  uint8_t bytes[EEPROM_STORED_SIZE];
  uint16_t i;
  uint16_t address;
  bytes[0] = (uint8_t)data->valid_mark;
  bytes[1] = (uint8_t)(data->valid_mark >> 8);
  bytes[2] = (uint8_t)(data->valid_mark >> 16);
  bytes[3] = (uint8_t)(data->valid_mark >> 24);
  bytes[4] = (uint8_t)data->light_threshold;
  bytes[5] = (uint8_t)(data->light_threshold >> 8);
  bytes[6] = (uint8_t)data->delay_time;
  bytes[7] = (uint8_t)(data->delay_time >> 8);
  bytes[8] = data->default_brightness;
  bytes[9] = (uint8_t)data->lighting_count;
  bytes[10] = (uint8_t)(data->lighting_count >> 8);
  bytes[11] = (uint8_t)(data->lighting_count >> 16);
  bytes[12] = (uint8_t)(data->lighting_count >> 24);
  bytes[13] = EEPROM_CalculateChecksum(data);

  for (i = 0; i < EEPROM_STORED_SIZE; i++)
  {
    address = EEPROM_DATA_ADDRESS + i;
    EEPROM_I2cStart();
    if (!EEPROM_I2cWriteByte(EEPROM_I2C_WRITE_ADDRESS) ||
        !EEPROM_I2cWriteByte((uint8_t)(address >> 8)) ||
        !EEPROM_I2cWriteByte((uint8_t)address) ||
        !EEPROM_I2cWriteByte(bytes[i]))
    {
      EEPROM_I2cStop();
      return HAL_ERROR;
    }
    EEPROM_I2cStop();
    HAL_Delay(5);
  }
  return HAL_OK;
}

/* 有效标志、校验和和参数范围都正确时，才使用 EEPROM 数据。 */
uint8_t EEPROM_IsDataValid(const EepromData_t *data)
{
  if (data->valid_mark != EEPROM_VALID_MARK) return 0;
  if (data->checksum != EEPROM_CalculateChecksum(data)) return 0;
  if (data->default_brightness > 100U) return 0;
  if (data->delay_time == 0U || data->delay_time > MAX_DELAY_TIME) return 0;
  if (data->light_threshold > ADC_FULL_SCALE) return 0;
  return 1;
}

/* ================= 最近 10 条运行记录存储区 =================
 * 参数区使用 0x0000 开始的 14 字节。
 * 字符串记录从 0x0100 开始，每条 84 字节，共 10 条，不会覆盖参数区。
 */
#define EEPROM_RECORD_BASE_ADDRESS   0x0100U
#define EEPROM_RECORD_COUNT          10U
#define EEPROM_RECORD_TEXT_SIZE      80U
#define EEPROM_RECORD_SIZE           84U
#define EEPROM_RECORD_VALID_MARK     0xA55AU

static uint8_t EEPROM_CalculateRecordChecksum(const EepromRecord_t *record)
{
  uint8_t sum = 0;
  uint8_t i;

  sum += (uint8_t)record->valid_mark;
  sum += (uint8_t)(record->valid_mark >> 8);
  for (i = 0; i < EEPROM_RECORD_TEXT_SIZE; i++)
    sum += (uint8_t)record->text[i];
  return sum;
}

static HAL_StatusTypeDef EEPROM_WriteBytes(uint16_t address, const uint8_t *bytes, uint16_t length)
{
  uint16_t i;

  for (i = 0; i < length; i++)
  {
    EEPROM_I2cStart();
    if (!EEPROM_I2cWriteByte(EEPROM_I2C_WRITE_ADDRESS) ||
        !EEPROM_I2cWriteByte((uint8_t)((address + i) >> 8)) ||
        !EEPROM_I2cWriteByte((uint8_t)(address + i)) ||
        !EEPROM_I2cWriteByte(bytes[i]))
    {
      EEPROM_I2cStop();
      return HAL_ERROR;
    }
    EEPROM_I2cStop();
    HAL_Delay(5);
  }
  return HAL_OK;
}

static HAL_StatusTypeDef EEPROM_ReadBytes(uint16_t address, uint8_t *bytes, uint16_t length)
{
  uint16_t i;

  EEPROM_I2cStart();
  if (!EEPROM_I2cWriteByte(EEPROM_I2C_WRITE_ADDRESS) ||
      !EEPROM_I2cWriteByte((uint8_t)(address >> 8)) ||
      !EEPROM_I2cWriteByte((uint8_t)address))
  {
    EEPROM_I2cStop();
    return HAL_ERROR;
  }

  EEPROM_I2cStart();
  if (!EEPROM_I2cWriteByte(EEPROM_I2C_WRITE_ADDRESS | 1U))
  {
    EEPROM_I2cStop();
    return HAL_ERROR;
  }

  for (i = 0; i < length; i++)
    bytes[i] = EEPROM_I2cReadByte(i + 1U < length);

  EEPROM_I2cStop();
  return HAL_OK;
}

HAL_StatusTypeDef EEPROM_SaveRecord(uint8_t index, const EepromRecord_t *record)
{
  uint8_t bytes[EEPROM_RECORD_SIZE];
  EepromRecord_t temp;
  uint8_t i;
  uint16_t address;

  if (index >= EEPROM_RECORD_COUNT || record == 0) return HAL_ERROR;

  temp = *record;
  temp.valid_mark = EEPROM_RECORD_VALID_MARK;
  temp.text[EEPROM_RECORD_TEXT_SIZE - 1U] = '\0';
  temp.checksum = EEPROM_CalculateRecordChecksum(&temp);

  bytes[0] = (uint8_t)temp.valid_mark;
  bytes[1] = (uint8_t)(temp.valid_mark >> 8);
  for (i = 0; i < EEPROM_RECORD_TEXT_SIZE; i++)
    bytes[2U + i] = (uint8_t)temp.text[i];
  bytes[82] = temp.checksum;
  bytes[83] = 0U;

  address = EEPROM_RECORD_BASE_ADDRESS + (uint16_t)index * EEPROM_RECORD_SIZE;
  return EEPROM_WriteBytes(address, bytes, EEPROM_RECORD_SIZE);
}

HAL_StatusTypeDef EEPROM_ReadRecord(uint8_t index, EepromRecord_t *record)
{
  uint8_t bytes[EEPROM_RECORD_SIZE];
  uint8_t i;
  uint16_t address;

  if (index >= EEPROM_RECORD_COUNT || record == 0) return HAL_ERROR;

  address = EEPROM_RECORD_BASE_ADDRESS + (uint16_t)index * EEPROM_RECORD_SIZE;
  if (EEPROM_ReadBytes(address, bytes, EEPROM_RECORD_SIZE) != HAL_OK)
    return HAL_ERROR;

  record->valid_mark = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
  for (i = 0; i < EEPROM_RECORD_TEXT_SIZE; i++)
    record->text[i] = (char)bytes[2U + i];
  record->text[EEPROM_RECORD_TEXT_SIZE - 1U] = '\0';
  record->checksum = bytes[82];
  return HAL_OK;
}

uint8_t EEPROM_IsRecordValid(const EepromRecord_t *record)
{
  if (record == 0) return 0U;
  if (record->valid_mark != EEPROM_RECORD_VALID_MARK) return 0U;
  if (record->text[0] == '\0') return 0U;
  return (record->checksum == EEPROM_CalculateRecordChecksum(record));
}
