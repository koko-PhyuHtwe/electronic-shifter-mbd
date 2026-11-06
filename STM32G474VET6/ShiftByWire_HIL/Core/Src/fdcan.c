/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    fdcan.c
  * @brief   This file provides code for the configuration
  *          of the FDCAN instances.
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
#include "fdcan.h"

/* USER CODE BEGIN 0 */
#include <string.h>

extern volatile uint8_t g_target_gear;
extern volatile uint32_t g_can_rx_tick;
extern volatile uint8_t g_vehicle_speed;
extern volatile uint8_t g_shift_inhibit;

/* 绿灯节流：0x180 每 10ms 一帧，50 帧翻转一次 ≈ 500ms 亮 / 500ms 灭 */
#define CAN_RX_LED_TOGGLE_FRAMES   ( 50U )

/* bus-off 自动恢复次数：串口可看，用来确认"拔插之后自己回来了" */
volatile uint32_t g_can_busoff_cnt = 0U;

/* ---- 对端重启 / 数据不可信 判定阈值：见 main.h ---- */
/* USER CODE END 0 */

FDCAN_HandleTypeDef hfdcan2;

/* FDCAN2 init function */
void MX_FDCAN2_Init(void)
{

  /* USER CODE BEGIN FDCAN2_Init 0 */

  /* USER CODE END FDCAN2_Init 0 */

  /* USER CODE BEGIN FDCAN2_Init 1 */

  /* USER CODE END FDCAN2_Init 1 */
  hfdcan2.Instance = FDCAN2;
  hfdcan2.Init.ClockDivider = FDCAN_CLOCK_DIV1;
  hfdcan2.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
  hfdcan2.Init.Mode = FDCAN_MODE_NORMAL;
  hfdcan2.Init.AutoRetransmission = DISABLE;
  hfdcan2.Init.TransmitPause = DISABLE;
  hfdcan2.Init.ProtocolException = DISABLE;
  hfdcan2.Init.NominalPrescaler = 8;
  hfdcan2.Init.NominalSyncJumpWidth = 4;
  hfdcan2.Init.NominalTimeSeg1 = 15;
  hfdcan2.Init.NominalTimeSeg2 = 4;
  hfdcan2.Init.DataPrescaler = 8;
  hfdcan2.Init.DataSyncJumpWidth = 4;
  hfdcan2.Init.DataTimeSeg1 = 15;
  hfdcan2.Init.DataTimeSeg2 = 4;
  hfdcan2.Init.StdFiltersNbr = 8;
  hfdcan2.Init.ExtFiltersNbr = 8;
  hfdcan2.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
  if (HAL_FDCAN_Init(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN FDCAN2_Init 2 */
  FDCAN_FilterTypeDef sFilterConfig = {0};
  sFilterConfig.IdType = FDCAN_STANDARD_ID;
  sFilterConfig.FilterIndex = 0;
  sFilterConfig.FilterType = FDCAN_FILTER_MASK;
  sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
  sFilterConfig.FilterID1 = 0x180;
  sFilterConfig.FilterID2 = 0x7FF;
  if (HAL_FDCAN_ConfigFilter(&hfdcan2, &sFilterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan2, FDCAN_REJECT, FDCAN_REJECT,
                                   FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK)
  {
    Error_Handler();
  }
  /* 同时打开总线关闭（Bus-Off）通知：
   * STM32 的 FDCAN 进 bus-off 后硬件会把 CCCR.INIT 置位，收发全部停摆，
   * 而且**不会自动恢复**——必须软件重启外设，否则只能整板复位。
   * 这就是"拔掉 CAN 线再插回去，通信再也回不来，但复位后一切正常"的根源。 */
  if (HAL_FDCAN_ActivateNotification(&hfdcan2,
                                     FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF,
                                     0) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_FDCAN_Start(&hfdcan2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE END FDCAN2_Init 2 */

}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef* fdcanHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(fdcanHandle->Instance==FDCAN2)
  {
  /* USER CODE BEGIN FDCAN2_MspInit 0 */

  /* USER CODE END FDCAN2_MspInit 0 */

  /** Initializes the peripherals clocks
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* FDCAN2 clock enable */
    __HAL_RCC_FDCAN_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**FDCAN2 GPIO Configuration
    PB12     ------> FDCAN2_RX
    PB13     ------> FDCAN2_TX
    */
    GPIO_InitStruct.Pin = CAN2_RX_Pin|CAN1_TX_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF9_FDCAN2;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* FDCAN2 interrupt Init */
    HAL_NVIC_SetPriority(FDCAN2_IT0_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(FDCAN2_IT0_IRQn);
  /* USER CODE BEGIN FDCAN2_MspInit 1 */

  /* USER CODE END FDCAN2_MspInit 1 */
  }
}

void HAL_FDCAN_MspDeInit(FDCAN_HandleTypeDef* fdcanHandle)
{

  if(fdcanHandle->Instance==FDCAN2)
  {
  /* USER CODE BEGIN FDCAN2_MspDeInit 0 */

  /* USER CODE END FDCAN2_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_FDCAN_CLK_DISABLE();

    /**FDCAN2 GPIO Configuration
    PB12     ------> FDCAN2_RX
    PB13     ------> FDCAN2_TX
    */
    HAL_GPIO_DeInit(GPIOB, CAN2_RX_Pin|CAN1_TX_Pin);

    /* FDCAN2 interrupt Deinit */
    HAL_NVIC_DisableIRQ(FDCAN2_IT0_IRQn);
  /* USER CODE BEGIN FDCAN2_MspDeInit 1 */

  /* USER CODE END FDCAN2_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
#include <stdio.h>

/* 0x180 的 B5：上层（S32K）给的电机油门百分比，0~100 */
volatile uint8_t g_motor_cmd = 0U;

/* 0x180 的 B6：上层（S32K）给的刹车力度百分比，0~100 */
volatile uint8_t g_brake_pct = 0U;

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
  FDCAN_RxHeaderTypeDef RxHeader = {0};
  uint8_t RxData[8];
  static uint8_t rxLedCnt   = 0U;
  static uint8_t s_lifeLast = 0U;
  static uint8_t s_lifeValid = 0U;
  static uint8_t s_speedLast = 0U;

  if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET)
  {
    if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &RxHeader, RxData) == HAL_OK)
    {
      if (RxHeader.Identifier == 0x180 && RxHeader.IdType == FDCAN_STANDARD_ID)
      {
        uint8_t gearNew  = RxData[0];
        uint8_t lifeNow  = RxData[3];
        uint8_t speedNow = RxData[4];

        /*
         * 对端重启检测（两条判据，任一成立即怀疑重启）：
         *   ① 车速单帧下跌 > 10km/h —— 物理上不可能，只能是数据源重启
         *   ② 生命计数不连续 且 车速恰好为 0 —— 重启后模型归零的典型特征
         *      加 "车速==0" 这一条是为了避免"总线偶发丢一帧"被误判成重启。
         * 只要当时车还在动（上次车速 > 5），就锁存"拒绝 P/R"。
         */
        if (s_lifeValid != 0U)
        {
          uint8_t lifeBroken  = (lifeNow != (uint8_t)(s_lifeLast + 1U)) ? 1U : 0U;
          uint8_t rebootSuspect = 0U;

          if ((s_speedLast > speedNow) &&
              ((uint8_t)(s_speedLast - speedNow) > SPEED_JUMP_MAX_KMH))
          {
            rebootSuspect = 1U;                 /* ① 车速暴跌 */
          }

          if ((lifeBroken != 0U) && (speedNow == 0U))
          {
            rebootSuspect = 1U;                 /* ② 计数断 + 车速归零 */
          }

          if ((s_speedLast > SPEED_STANDSTILL_KMH) && (rebootSuspect != 0U))
          {
            g_shift_inhibit = 1U;   /* 锁存，直到断电重启 */
          }
        }

        s_lifeValid  = 1U;
        s_lifeLast   = lifeNow;
        s_speedLast  = speedNow;

        g_vehicle_speed = speedNow;
        g_target_gear   = gearNew;
        g_motor_cmd     = RxData[5];      /* B5 = 电机油门 %（0~100） */
        g_brake_pct     = RxData[6];      /* B6 = 刹车 %（0~100），执行侧据此做刹车优先 */
        g_can_rx_tick   = HAL_GetTick();

        rxLedCnt++;
        if (rxLedCnt >= CAN_RX_LED_TOGGLE_FRAMES)
        {
          rxLedCnt = 0U;
          HAL_GPIO_TogglePin(GREEN_GPIO_Port, GREEN_Pin);
        }
      }
    }
  }
}

/**
  * @brief  错误状态回调：负责 bus-off 自愈
  * @note   STM32G4 的 FDCAN 进 bus-off 后，硬件把 CCCR.INIT 置 1、收发全部停摆，
  *         并且不会自己退出——必须软件 Stop/Start 一次。
  *         不处理的表现：拔掉 CAN 线（发送无人 ACK → TEC 涨到 256 → bus-off），
  *         插回去之后一帧都发不出去、也收不到，绿灯不闪，只有复位能救。
  *         处理之后：拔插自动恢复，串口 g_can_busoff_cnt 加一。
  */
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
  if (hfdcan->Instance != FDCAN2)
  {
    return;
  }

  if ((ErrorStatusITs & FDCAN_IT_BUS_OFF) != 0U)
  {
    g_can_busoff_cnt++;

    /* 重新进初始化再退出：清掉 bus-off 状态，硬件随后等 128×11 个隐性位
     * 就从 bus-off 恢复，计数器归零，收发恢复。
     * 注意：这里在中断里，不能走 Error_Handler() 死循环——重启失败就维持
     * 原状，让上层状态位（通信超时）继续把它按安全状态处理。 */
    if (HAL_FDCAN_Stop(hfdcan) == HAL_OK)
    {
      (void)HAL_FDCAN_Start(hfdcan);
    }

    /* Stop 会关掉中断使能，重新打开（含 bus-off，保证下次还能自愈） */
    (void)HAL_FDCAN_ActivateNotification(hfdcan,
                                         FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_BUS_OFF,
                                         0);
  }
}
/* USER CODE END 1 */

