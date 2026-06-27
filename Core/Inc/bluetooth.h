#ifndef __BLUETOOTH_H
#define __BLUETOOTH_H

#include "main.h"

/* Bluetooth command callback: (command_string, length) */
typedef void (*BT_CmdCallback_t)(char *cmd, uint16_t len);

#define UART_BUFFER_SIZE  128U

void Bluetooth_Init(void);
void Bluetooth_Process(void);
void Bluetooth_Send(const char *text);

/* Idle interrupt callback - called from HAL_UARTEx_RxEventCallback */
void Bluetooth_RxIdleCallback(UART_HandleTypeDef *huart, uint16_t size);

/* Legacy compatibility - kept for main.c ISR routing */
void Bluetooth_RxByteCallback(uint8_t received_byte);
void Bluetooth_UartRxComplete(void);

#endif
