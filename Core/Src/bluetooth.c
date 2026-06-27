/**
 * @file bluetooth.c
 * @brief 信驰达低功耗蓝牙模块驱动
 *
 * 硬件连接 (信驰达 BLE 模块):
 *   PA2 (USART2_TX)  --> 蓝牙 RX   (MCU发送给模块)
 *   PA3 (USART2_RX)  <-- 蓝牙 TX    (模块发送给MCU)
 *   PB0 (BT_EN)      --> 蓝牙 EN     (低电平有效: 使能广播/连接)
 *   PB1 (BT_BRTS)    --> 蓝牙 BRTS   (低电平有效: 主机发送请求)
 *
 * 关键协议要求:
 *   - EN 必须拉低才能广播和连接
 *   - 发送数据前必须将 BRTS 拉低(>=50ms)，发送完毕后拉高
 *   - 连接成功时模块输出 "TTM:CONNECTED\r\n"
 */
#include "bluetooth.h"
#include "app.h"
#include "gpio.h"
#include "peripherals.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========== Receive buffer ========== */
static uint8_t uart2_rx_buf[UART_BUFFER_SIZE] = {0};
static volatile uint16_t uart2_rx_len = 0;
static volatile uint8_t uart2_rx_complete = 0;

/* ========== Send state ========== */
static volatile uint8_t tx_busy = 0;
static uint32_t bt_send_count = 0U;       /* Total sends attempted */
static uint32_t bt_send_ok_count = 0U;    /* Successful sends */
static uint32_t bt_send_fail_count = 0U;  /* Failed sends */

/* ========== Redirect printf to USART1 (PC debug) ========== */
int fputc(int ch, FILE *f)
{
  HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 10);
  return ch;
}

/* ========== Idle interrupt callback (primary RX path) ========== */
void Bluetooth_RxIdleCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
  if (huart->Instance == USART2)
  {
    uart2_rx_len = (Size > UART_BUFFER_SIZE) ? UART_BUFFER_SIZE : Size;
    uart2_rx_complete = 1;
    HAL_UARTEx_ReceiveToIdle_IT(&huart2, uart2_rx_buf, UART_BUFFER_SIZE);
  }
}

/* ========== Initialize Bluetooth module ========== */
void Bluetooth_Init(void)
{
  /* EN and BRTS GPIO are initialized in MX_GPIO_Init() (gpio.c)
   * EN is already LOW (active), BRTS is HIGH (idle) */

  memset(uart2_rx_buf, 0, UART_BUFFER_SIZE);
  uart2_rx_len = 0;
  uart2_rx_complete = 0;
  tx_busy = 0;
  bt_send_count = 0;
  bt_send_ok_count = 0;
  bt_send_fail_count = 0;

  /* Start idle interrupt reception */
  HAL_UARTEx_ReceiveToIdle_IT(&huart2, uart2_rx_buf, UART_BUFFER_SIZE);

  printf("[BT] Init OK, baud=115200, EN=LOW(active), BRTS=HIGH(idle)\r\n");
}

/* ========== Legacy stubs (main.c compatibility) ========== */
void Bluetooth_RxByteCallback(uint8_t received_byte)
{
  (void)received_byte;
}
void Bluetooth_UartRxComplete(void)
{
  /* No-op */
}

/* ========== Main loop: process received commands ========== */
void Bluetooth_Process(void)
{
  if (!uart2_rx_complete) return;
  uart2_rx_complete = 0;

  if (uart2_rx_len > 0U && uart2_rx_len < UART_BUFFER_SIZE)
    uart2_rx_buf[uart2_rx_len] = '\0';
  else
    uart2_rx_buf[UART_BUFFER_SIZE - 1] = '\0';

  printf("[BLE] RX(%u): %s\r\n", uart2_rx_len, (char *)uart2_rx_buf);
  App_HandleBluetoothCommand((char *)uart2_rx_buf);

  memset(uart2_rx_buf, 0, uart2_rx_len);
  uart2_rx_len = 0;
}

/* ========== Core send function with BRTS flow control ========== */
/**
 * 信驰达模块发送流程（文档要求）：
 *   1. BRTS 拉低 -> 通知模块准备接收
 *   2. 延迟 >=50ms
 *   3. 通过串口TX发送数据
 *   4. 等待发送完成(TC标志)
 *   5. BRTS 拉高 -> 通知模块接收完毕
 */
static void BT_SendToModule(const char *text)
{
  uint16_t len;
  HAL_StatusTypeDef status;
  uint32_t start_tick;

  if (text == NULL) return;
  len = (uint16_t)strlen(text);
  if (len == 0) return;

  bt_send_count++;

  /* Wait for previous send with timeout (1 second max) */
  start_tick = HAL_GetTick();
  while (tx_busy)
  {
    if (HAL_GetTick() - start_tick > 1000U)
    {
      printf("[BT] WARN: mutex timeout, force unlock\r\n");
      tx_busy = 0;
      HAL_UART_AbortTransmit(&huart2);
      huart2.gState = HAL_UART_STATE_READY;
      break;
    }
    HAL_Delay(1);
  }

  tx_busy = 1;

  /* ===== Step 1: Pull BRTS LOW (request to send) ===== */
  HAL_GPIO_WritePin(BT_BRTS_GPIO_PORT, BT_BRTS_GPIO_PIN, GPIO_PIN_RESET);

  /* ===== Step 2: Wait >=50ms for module ready ===== */
  HAL_Delay(55);  /* Slightly more than 50ms per spec */

  /* Clear TC flag before transmit */
  __HAL_UART_CLEAR_FLAG(&huart2, UART_FLAG_TC);

  /* Small delay for hardware stability */
  for (volatile int i = 0; i < 100; i++);

  /* ===== Step 3: Transmit via USART2 ===== */
  status = HAL_UART_Transmit(&huart2, (const uint8_t *)text, len, 500);

  if (status != HAL_OK)
  {
    bt_send_fail_count++;
    printf("[BT] FAIL#%lu: HAL error=%d\r\n",
           (unsigned long)bt_send_fail_count, (int)status);
    /* Still release BRTS on error! */
    HAL_GPIO_WritePin(BT_BRTS_GPIO_PORT, BT_BRTS_GPIO_PIN, GPIO_PIN_SET);
    HAL_UART_AbortTransmit(&huart2);
    huart2.gState = HAL_UART_STATE_READY;
    tx_busy = 0;
    return;
  }

  /* ===== Step 4: Wait for Transmission Complete flag ===== */
  start_tick = HAL_GetTick();
  while (!__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC))
  {
    if (HAL_GetTick() - start_tick > 500U)
    {
      bt_send_fail_count++;
      printf("[BT] FAIL#%lu: TC timeout\r\n", (unsigned long)bt_send_fail_count);
      __HAL_UART_CLEAR_FLAG(&huart2, UART_FLAG_TC);
      HAL_GPIO_WritePin(BT_BRTS_GPIO_PORT, BT_BRTS_GPIO_PIN, GPIO_PIN_SET);
      tx_busy = 0;
      return;
    }
  }
  __HAL_UART_CLEAR_FLAG(&huart2, UART_FLAG_TC);

  /* ===== Step 5: Pull BRTS HIGH (send complete) ===== */
  HAL_GPIO_WritePin(BT_BRTS_GPIO_PORT, BT_BRTS_GPIO_PIN, GPIO_PIN_SET);

  bt_send_ok_count++;
  tx_busy = 0;


}

/* ========== Public API: only send to Bluetooth module ========== */
void Bluetooth_Send(const char *text)
{
  if (text == NULL) return;
  if (strlen(text) == 0U) return;

  /* 蓝牙回复只发到 USART2，不再回显到 USART1，避免电脑串口输出混乱。 */
  BT_SendToModule(text);
}
