#ifndef __EEPROM_H
#define __EEPROM_H

#include "main.h"

typedef struct
{
  uint32_t valid_mark;
  uint16_t light_threshold;
  uint16_t delay_time;
  uint8_t default_brightness;
  uint32_t lighting_count;
  uint8_t checksum;
} EepromData_t;

/* 最近 10 条运行记录，用于 GET E 查询，直接保存 IPOP 输出的整行字符串。 */
typedef struct
{
  uint16_t valid_mark;
  char text[80];
  uint8_t checksum;
} EepromRecord_t;

HAL_StatusTypeDef EEPROM_ReadData(EepromData_t *data);
HAL_StatusTypeDef EEPROM_SaveData(const EepromData_t *data);
uint8_t EEPROM_IsDataValid(const EepromData_t *data);
HAL_StatusTypeDef EEPROM_SaveRecord(uint8_t index, const EepromRecord_t *record);
HAL_StatusTypeDef EEPROM_ReadRecord(uint8_t index, EepromRecord_t *record);
uint8_t EEPROM_IsRecordValid(const EepromRecord_t *record);
void EEPROM_Init(void);

#endif
