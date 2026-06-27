#ifndef __OLED_H
#define __OLED_H

#include "main.h"

/* OLED 中文字库编号。每个汉字使用 16x16 点阵。 */
typedef enum
{
  OLED_HZ_GUANG = 0, OLED_HZ_XIAN, OLED_HZ_WEN, OLED_HZ_DU,
  OLED_HZ_LIANG, OLED_HZ_YU, OLED_HZ_ZHUANG, OLED_HZ_TAI,
  OLED_HZ_DAI, OLED_HZ_JI, OLED_HZ_ZHAO, OLED_HZ_MING,
  OLED_HZ_GUO, OLED_HZ_RE, OLED_HZ_CAN, OLED_HZ_SHU,
  OLED_HZ_SHE, OLED_HZ_ZHI, OLED_HZ_YAN, OLED_HZ_SHI,
  OLED_HZ_MIAO, OLED_HZ_GAN, OLED_HZ_YING, OLED_HZ_TONG,
  OLED_HZ_JI2, OLED_HZ_DENG, OLED_HZ_CI, OLED_HZ_AN,
  OLED_HZ_JIAN, OLED_HZ_QING, OLED_HZ_LING, OLED_HZ_SHENG,
  OLED_HZ_COUNT
} OLED_Chinese_t;

void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowString(uint8_t x, uint8_t page, const char *text);
void OLED_ShowString16(uint8_t x, uint8_t page, const char *text);
void OLED_ShowChinese(uint8_t x, uint8_t page, OLED_Chinese_t chinese);
void OLED_Refresh(void);

#endif
