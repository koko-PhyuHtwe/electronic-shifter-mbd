/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "fdcan.h"
#include "usart.h"
#include "tim.h"
#include "iwdg.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern volatile uint8_t g_target_gear;
extern volatile uint8_t g_actual_gear;
extern volatile uint8_t g_servo_angle;
extern volatile uint8_t g_status;
extern volatile uint32_t g_can_rx_tick;
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 128 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
void vTask_CAN_Tx(void *argument);
void vTask_Servo_Control(void *argument);
void vTask_Debug(void *argument);
void vTask_LED(void *argument);
static uint8_t GearToAngle(uint8_t gear);
/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  osThreadNew(vTask_CAN_Tx, NULL, NULL);
  osThreadNew(vTask_Servo_Control, NULL, NULL);
  osThreadNew(vTask_Debug, NULL, NULL);
  osThreadNew(vTask_LED, NULL, NULL);
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
static uint8_t GearToAngle(uint8_t gear)
{
  switch (gear)
  {
    case 0:  return 0;     /* P */
    case 1:  return 36;    /* R */
    case 2:  return 72;    /* N */
    case 3:  return 108;   /* D */
    case 11: case 12: case 13: case 14: case 15: case 16: return 108; /* M1-M6 */
    default: return 72;    /* invalid -> keep N */
  }
}

void vTask_Servo_Control(void *argument)
{
  uint8_t last_gear = g_target_gear;

  for (;;)
  {
    /* 通信超时保护：超过 500ms 未收到 0x180 判定通信中断 */
    if ((HAL_GetTick() - g_can_rx_tick) > 500U)
    {
      g_status |= 0x01U;   /* bit0 = 通信超时故障，进入 fail-safe */
    }
    else
    {
      g_status &= ~0x01U;  /* 通信恢复，清除故障标志 */
    }

    /* 仅在通信正常时响应新目标档；超时期间保持当前档（fail-safe） */
    if ((g_target_gear != last_gear) && ((g_status & 0x01U) == 0U))
    {
      last_gear = g_target_gear;
      g_servo_angle = GearToAngle(last_gear);
      /* 500us ~ 2500us mapping, 0~180 deg */
      uint16_t pulse = 500U + ((uint16_t)g_servo_angle * 100U) / 9U;
      __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse);
      g_actual_gear = last_gear;
    }
    osDelay(10);
  }
}

void vTask_CAN_Tx(void *argument)
{
  FDCAN_TxHeaderTypeDef TxHeader = {0};
  uint8_t TxData[8];
  uint8_t life_counter = 0;

  TxHeader.Identifier = 0x280;
  TxHeader.IdType = FDCAN_STANDARD_ID;
  TxHeader.TxFrameType = FDCAN_DATA_FRAME;
  TxHeader.DataLength = FDCAN_DLC_BYTES_8;
  TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
  TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
  TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
  TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
  TxHeader.MessageMarker = 0;

  memset(TxData, 0, sizeof(TxData));

  for (;;)
  {
    TxData[0] = g_actual_gear;
    TxData[1] = g_servo_angle;
    TxData[2] = g_status;
    TxData[3] = life_counter++;

    HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &TxHeader, TxData);
    HAL_IWDG_Refresh(&hiwdg);

    osDelay(10);
  }
}

void vTask_Debug(void *argument)
{
  for (;;)
  {
    printf("Target:%d Actual:%d Angle:%d Status:%d\r\n",
           g_target_gear, g_actual_gear, g_servo_angle, g_status);
    osDelay(100);
  }
}

void vTask_LED(void *argument)
{
  for (;;)
  {
    HAL_GPIO_TogglePin(BLUE_GPIO_Port, BLUE_Pin);
    osDelay(1000);
  }
}
/* USER CODE END Application */

