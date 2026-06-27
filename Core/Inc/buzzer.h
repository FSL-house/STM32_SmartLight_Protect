#ifndef __BUZZER_H
#define __BUZZER_H

#include "main.h"

void Buzzer_Init(void);
void Buzzer_Beep(uint8_t times);
void Buzzer_SetOverheat(uint8_t enabled);
void Buzzer_Process(void);

#endif
