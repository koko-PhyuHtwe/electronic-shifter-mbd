/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */
void Motor_Init(void);
void Motor_SetThrottlePct(uint8_t pct);
extern volatile uint8_t g_motor_throttle;
extern volatile uint8_t g_motor_cmd;
extern volatile uint8_t g_brake_pct;
extern volatile uint32_t g_can_busoff_cnt;   /* bus-off 自愈次数，端口可观测 */

/* ---- 车速相关阈值（fdcan.c 用来判对端重启，app_freertos.c 用来拦 P/R）---- */
#define SPEED_STANDSTILL_KMH   (  5U )   /* 低于此速度视为已停车 */
#define SPEED_JUMP_MAX_KMH     ( 10U )   /* 单帧(10ms)车速下跌超过此值 → 不可能是真刹车 */
#define PARK_BRAKE_MIN_PCT     ( 30U )   /* 挂 P 挡要求的最低刹车力度 %，和 S32K 侧一致 */
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define CAN2_RX_Pin GPIO_PIN_12
#define CAN2_RX_GPIO_Port GPIOB
#define CAN1_TX_Pin GPIO_PIN_13
#define CAN1_TX_GPIO_Port GPIOB
#define BLUE_Pin GPIO_PIN_0
#define BLUE_GPIO_Port GPIOE
#define GREEN_Pin GPIO_PIN_1
#define GREEN_GPIO_Port GPIOE

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
