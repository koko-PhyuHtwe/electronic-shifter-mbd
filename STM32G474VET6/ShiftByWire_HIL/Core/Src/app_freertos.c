/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : 线控换挡执行侧 FreeRTOS 任务
  *                      0x180 收目标档 → 舵机摆到对应角度 → 0x280 回传实际档
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
/* ---- 舵机 ---- */
#define SERVO_TIM_CHANNEL       TIM_CHANNEL_1
#define SERVO_PULSE_MIN_US      ( 500U )    /* 0°   对应的脉宽 */

/* 舵机角度 → 档位 ---- */
#define SERVO_ANGLE_MAX_DEG     ( 180U )    /* 脉宽跨度对应的总角度 */
#define SERVO_ANGLE_P           (   0U )    /* P */
#define SERVO_ANGLE_R           (  36U )    /* R */
#define SERVO_ANGLE_N           (  72U )    /* N */
#define SERVO_ANGLE_D           ( 108U )    /* D / M1~M6 */

/* ---- CAN ---- */
#define CAN_ID_CMD_GEAR         ( 0x180U )  /* 收：目标档 */
#define CAN_ID_STATUS           ( 0x280U )  /* 发：实际档 + 状态 */

/* ---- 电机（好盈 20A 电调 + X2212 无刷）------------------------------------
 * 电调输入就是标准 RC PWM：周期 20ms（50Hz）、脉宽 1000~2000us。
 * TIM3 已是 PSC=79(1MHz) / ARR=19999(50Hz)，CCR 单位正好是 us，可直接写脉宽。
 * 注意：绝不能复用 ServoAngleToPulse()，那是 500~2500us 的舵机行程，
 *       500us 超出电调合法范围，部分型号会判为无效信号拒绝解锁。
 */
#define MOTOR_TIM_CHANNEL       TIM_CHANNEL_2
#define MOTOR_PULSE_MIN_US      (1000U)     /* 0%   油门 / 上电解锁脉宽 */
#define MOTOR_PULSE_MAX_US      (2000U)     /* 100% 油门 */
#define MOTOR_ARM_HOLD_MS       (3000U)     /* 上电保持在最小油门的时长，等电调解锁 */
#define MOTOR_TASK_PERIOD_MS    (  10U )
#define MOTOR_CMD_MAX_PCT       (  60U )    /* 台架限幅：0x180 的油门字节最多认到 60% */
#define MOTOR_OUT_RAMP_PCT      (   3U )    /* 输出斜坡：每 10ms 最多变 3% → 满油门 200ms 到位 */

/* ---------------- 电调排查开关（正常跑演示时全部保持 0） ----------------
 * MOTOR_CALIB_MODE : 上电先给满油门 5s 再回最小油门，跑一遍"油门行程校准"。
 *                    电调不是原厂默认行程时（换过遥控器/被编程过）用它。
 * MOTOR_TEST_MODE  : 不看 CAN、不看踏板，直接输出 MOTOR_TEST_PCT 的油门，
 *                    用来区分"命令链路问题"和"电调/接线问题"。
 * 注意：这两个模式都会让电机动，测试时必须拆桨！
 */
#define MOTOR_CALIB_MODE        (0U)
#define MOTOR_TEST_MODE         (0U)
#define MOTOR_TEST_PCT          (20U)

/* ---- 任务周期 ---- */
#define SERVO_TASK_PERIOD_MS    (  10U )
#define CAN_TX_PERIOD_MS        (  10U )
#define DEBUG_PERIOD_MS         ( 100U )
#define LED_PERIOD_MS           ( 1000U )

/* ---- 保护 ---- */
#define CAN_RX_TIMEOUT_MS       ( 500U )    /* 超过这么久没收到 0x180 判通信中断 */
#define GEAR_INVALID            ( 255U )    /* 无效档位/初值 */

/* ---- g_status 位定义 ---- */
#define STATUS_COMM_TIMEOUT     ( 0x01U )   /* bit0: 通信超时，执行侧 fail-safe */
#define STATUS_SHIFT_REFUSED    ( 0x02U )   /* bit1: 拒绝执行 P/R（对端行驶中重启过） */
#define STATUS_INVALID_CMD      ( 0x04U )   /* bit2: 收到的档位码非法（如 FAULT 时的 255），保持不动 */
#define STATUS_MOTOR_CUT        ( 0x08U )   /* bit3: 动力被软切（失联/FAULT），油门已归零并锁存 */
#define STATUS_GEAR_INHIBIT     ( 0x10U )   /* bit4: 当前挡位不给动力（P/N/R），油门请求被拒绝 */
#define STATUS_BRAKE_CUT        ( 0x20U )   /* bit5: 刹车优先，油门请求被切断 */

/* ---- 电机 ---- */
#define BRAKE_CUT_PCT           (  5U )     /* 刹车超过这个力度就断动力（留一点噪声余量） */

/* ---- 档位编码（与 S32K 侧一致）---- */
#define GEAR_P                  (  0U )
#define GEAR_R                  (  1U )
#define GEAR_N                  (  2U )
#define GEAR_D                  (  3U )
#define GEAR_M1                 ( 11U )
#define GEAR_M6                 ( 16U )
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern volatile uint8_t  g_target_gear;
extern volatile uint8_t  g_actual_gear;
extern volatile uint8_t  g_servo_angle;
extern volatile uint8_t  g_status;
extern volatile uint32_t g_can_rx_tick;
extern volatile uint8_t  g_vehicle_speed;
extern volatile uint8_t  g_shift_inhibit;
extern volatile uint8_t  g_motor_cmd;             /* 0x180 的 B5：上层给的油门 % */
extern volatile uint8_t  g_brake_pct;             /* 0x180 的 B6：上层给的刹车 % */
volatile uint8_t         g_motor_throttle = 0U;   /* 0~100%，实际下发给电调的值 */

/* 电机解锁状态：最小油门持续够久才放行，防止电调上电进编程模式 */
static uint8_t           s_motorArmed  = 0U;
static uint32_t          s_motorArmCnt = 0U;
/* 上电安全：见过一次"油门=0"之后才允许往外送油门 */
static uint8_t           s_motorSawZero = 0U;
/* 软切：失联/FAULT 时锁存，踏板必须回到 0 才允许重新给油 */
static uint8_t           s_motorCutLatch = 0U;
/* 斜坡后的实际输出（0~100） */
static uint8_t           s_motorOutPct = 0U;
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
static uint8_t  GearToAngle(uint8_t gear);
static uint8_t  IsValidGear(uint8_t gear);
void Motor_Init(void);
void Motor_SetThrottlePct(uint8_t pct);
static void Motor_ArmTick(void);
static uint16_t ServoAngleToPulse(uint8_t angleDeg);
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
  (void)argument;

  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  档位码是否合法
  * @note   合法 = P(0)/R(1)/N(2)/D(3)/M1~M6(11~16)。
  *         非法码（典型是 S32K 进 FAULT 时发的 255）必须区别对待：
  *         "收到非法码"不等于"要求去 N"，执行侧应当保持不动。
  */
static uint8_t IsValidGear(uint8_t gear)
{
  if ((gear <= 3U) || ((gear >= 11U) && (gear <= 16U)))
  {
    return 1U;
  }
  return 0U;
}

/**
  * @brief  档位码 → 舵机角度
  * @note   P=0° R=36° N=72° D=108°，M1~M6 与 D 共用 108°。
  *         非法码已由 IsValidGear() 提前拦掉，不会走到 default 分支；
  *         default 保留 N 只作最终兜底。
  */
static uint8_t GearToAngle(uint8_t gear)
{
  switch (gear)
  {
    case 0:  return SERVO_ANGLE_P;
    case 1:  return SERVO_ANGLE_R;
    case 2:  return SERVO_ANGLE_N;
    case 3:  return SERVO_ANGLE_D;
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16: return SERVO_ANGLE_D;
    default: return SERVO_ANGLE_N;
  }
}

/**
  * @brief  角度 → PWM 脉宽（µs）
  * @note   TIM3 已配成 50Hz、计数器时钟 1MHz，所以 CCR 的单位就是 µs：
  *         0° → 500µs，180° → 2500µs，线性插值。
  */
static uint16_t ServoAngleToPulse(uint8_t angleDeg)
{
  if (angleDeg > SERVO_ANGLE_MAX_DEG)
  {
    angleDeg = SERVO_ANGLE_MAX_DEG;
  }

  /* 跨度 = 2500 - 500 = 2000µs 对应 180° */
  return (uint16_t)(SERVO_PULSE_MIN_US
                    + ((uint32_t)angleDeg * 2000U) / SERVO_ANGLE_MAX_DEG);
}

/**
  * @brief  电机初始化：先把电调拉到最小油门再开通道
  * @note   必须在 osKernelStart() 之前调用。好盈电调上电时若收到高油门，
  *         会进入油门行程设定/编程模式，之后不认正常指令，所以开局必须是 1000µs。
  */
void Motor_Init(void)
{
#if (MOTOR_CALIB_MODE != 0U)
  /* 油门行程校准：满油门 → 电调长鸣确认最大行程 → 5s 后拉回最小油门 → 短鸣确认 */
  printf("[MOTOR] calibrating: MAX 2000us for 5s, then MIN 1000us\r\n");
  __HAL_TIM_SET_COMPARE(&htim3, MOTOR_TIM_CHANNEL, MOTOR_PULSE_MAX_US);
  HAL_TIM_PWM_Start(&htim3, MOTOR_TIM_CHANNEL);
  HAL_Delay(5000U);
  __HAL_TIM_SET_COMPARE(&htim3, MOTOR_TIM_CHANNEL, MOTOR_PULSE_MIN_US);
  HAL_Delay(1000U);
  printf("[MOTOR] calibration done, power-cycle the ESC\r\n");
  s_motorArmed  = 1U;      /* 两端都走过，直接解锁 */
  s_motorArmCnt = 0U;
#else
  __HAL_TIM_SET_COMPARE(&htim3, MOTOR_TIM_CHANNEL, MOTOR_PULSE_MIN_US);
  HAL_TIM_PWM_Start(&htim3, MOTOR_TIM_CHANNEL);
  s_motorArmed  = 0U;
  s_motorArmCnt = 0U;
#endif
}

/**
  * @brief  油门百分比(0~100) → 电调脉宽
  * @note   未解锁前一律输出最小脉宽，避免电调在半路被"惊到"。
  */
void Motor_SetThrottlePct(uint8_t pct)
{
  uint32_t pulse;
  uint32_t span = (uint32_t)(MOTOR_PULSE_MAX_US - MOTOR_PULSE_MIN_US);

  if (s_motorArmed == 0U)
  {
    pulse = MOTOR_PULSE_MIN_US;
  }
  else
  {
    if (pct > 100U)
    {
      pct = 100U;
    }
    pulse = (uint32_t)MOTOR_PULSE_MIN_US + ((uint32_t)pct * span) / 100U;
  }

  __HAL_TIM_SET_COMPARE(&htim3, MOTOR_TIM_CHANNEL, (uint16_t)pulse);
}

/**
  * @brief  解锁计时：最小油门持续 MOTOR_ARM_HOLD_MS 后放行油门
  * @note   每 MOTOR_TASK_PERIOD_MS 调一次（放在 10ms 的舵机任务里）。
  */
static void Motor_ArmTick(void)
{
  if (s_motorArmed != 0U)
  {
    return;
  }

  if (++s_motorArmCnt >= (MOTOR_ARM_HOLD_MS / MOTOR_TASK_PERIOD_MS))
  {
    s_motorArmed = 1U;
  }
}

/**
  * @brief  舵机任务：跟目标档，通信超时则保持原位（fail-safe）
  */
void vTask_Servo_Control(void *argument)
{
  /* 初值给无效档位，保证收到第一帧有效目标时一定会动一次 */
  uint8_t lastGear = GEAR_INVALID;

  (void)argument;

  /* 上电先把舵机推到 P 档，避免停在硬件默认角度 */
  g_servo_angle = GearToAngle(0U);
  __HAL_TIM_SET_COMPARE(&htim3, SERVO_TIM_CHANNEL, ServoAngleToPulse(g_servo_angle));
  g_actual_gear = 0U;

  for (;;)
  {
    /* 电机：先走解锁计时，再把上层油门变成电调脉宽 */
    Motor_ArmTick();
    /* 上电安全：必须先在总线上见过一次"油门=0"，才允许输出。
     * 否则踏板悬空/未校准时报个非零值，电机一上电就自己转起来。 */
    if (g_motor_cmd == 0U)
    {
      s_motorSawZero = 1U;
    }
    {
      uint8_t commLost = ((HAL_GetTick() - g_can_rx_tick) > CAN_RX_TIMEOUT_MS) ? 1U : 0U;
      uint8_t faultCmd = (g_target_gear == GEAR_INVALID) ? 1U : 0U;
      uint8_t target;

      /* 软切：通信丢失或进入 FAULT 时，目标油门立刻打成 0，输出按斜坡降下来。
       * 锁存后必须等踏板回到 0 才允许重新给油，避免"抖动一下又满油门"。
       * 对动力而言，"失联就保持"是危险的，和舵机的策略正好相反。 */
      if ((commLost != 0U) || (faultCmd != 0U))
      {
        s_motorCutLatch = 1U;
      }
      if ((s_motorCutLatch != 0U) && (g_motor_cmd == 0U))
      {
        s_motorCutLatch = 0U;
      }

#if (MOTOR_TEST_MODE != 0U)
      target = MOTOR_TEST_PCT;      /* 排查：不看 CAN，直接给固定油门（也跳过挡位门） */
#else
      target = ((s_motorSawZero != 0U) && (s_motorCutLatch == 0U)) ? g_motor_cmd : 0U;

      /* 挡位门：只有 D / M1~M6 才把踏板当驱动力。
       * P = 停驻、N = 空挡、R = 需要反转（台架电调单向做不到）→ 一律不给动力。
       * 用 g_actual_gear（舵机真正到位）而不是 target：还在往 D 转的过程中就加油，
       * 等于"没挂上挡就冲出去"。 */
      if ((g_actual_gear == GEAR_D) ||
          ((g_actual_gear >= GEAR_M1) && (g_actual_gear <= GEAR_M6)))
      {
        g_status = (uint8_t)(g_status & (uint8_t)(~STATUS_GEAR_INHIBIT));
      }
      else
      {
        g_status = (uint8_t)(g_status | STATUS_GEAR_INHIBIT);
        target   = 0U;
      }

      /* 刹车优先：刹车和油门同时踩下时，动力必须让位。
       * 真车也是这个规矩——刹车灯亮着还给动力，是最典型的" unintended acceleration "。
       * 不做锁存：松开刹车立刻恢复，不需要额外动作。 */
      if (g_brake_pct > BRAKE_CUT_PCT)
      {
        g_status = (uint8_t)(g_status | STATUS_BRAKE_CUT);
        target   = 0U;
      }
      else
      {
        g_status = (uint8_t)(g_status & (uint8_t)(~STATUS_BRAKE_CUT));
      }
#endif

      if (target > MOTOR_CMD_MAX_PCT)
      {
        target = MOTOR_CMD_MAX_PCT;   /* 台架限幅，别一推到底 */
      }

      /* 斜坡：每 MOTOR_TASK_PERIOD_MS 最多变 MOTOR_OUT_RAMP_PCT，上下都限速 */
      {
        uint8_t diff = (uint8_t)((s_motorOutPct < target)
                                 ? (target - s_motorOutPct)
                                 : (s_motorOutPct - target));

        if (diff > (uint8_t)MOTOR_OUT_RAMP_PCT)
        {
          diff = (uint8_t)MOTOR_OUT_RAMP_PCT;
        }

        if (s_motorOutPct < target)
        {
          s_motorOutPct = (uint8_t)(s_motorOutPct + diff);
        }
        else
        {
          s_motorOutPct = (uint8_t)(s_motorOutPct - diff);
        }
      }

      g_motor_throttle = s_motorOutPct;      /* 供串口打印，便于对照 */
      Motor_SetThrottlePct(s_motorOutPct);

      if (s_motorCutLatch != 0U)
      {
        g_status |= STATUS_MOTOR_CUT;
      }
      else
      {
        g_status &= (uint8_t)(~STATUS_MOTOR_CUT);
      }
    }
    /* 通信看门狗：超过 500ms 没收到 0x180 就置故障位 */
    if ((HAL_GetTick() - g_can_rx_tick) > CAN_RX_TIMEOUT_MS)
    {
      g_status |= STATUS_COMM_TIMEOUT;
    }
    else
    {
      g_status &= (uint8_t)(~STATUS_COMM_TIMEOUT);
    }

    /* 拒绝执行标志（锁存）：只要置位就一直上报，S32K 侧屏幕显示 EXEC LOCK */
    if (g_shift_inhibit != 0U)
    {
      g_status |= STATUS_SHIFT_REFUSED;
    }
    else
    {
      g_status &= (uint8_t)(~STATUS_SHIFT_REFUSED);
    }

    /* 非法指令标志：每圈先清，收到非法码的那一圈才点亮 */
    g_status &= (uint8_t)(~STATUS_INVALID_CMD);

    /* 只在通信正常时响应新目标；超时期间保持当前角度 */
    if ((g_target_gear != lastGear) && ((g_status & STATUS_COMM_TIMEOUT) == 0U))
    {
      if (IsValidGear(g_target_gear) == 0U)
      {
        /*
         * 非法档位码（典型：S32K 进 FAULT 时发的 255）。
         * 关键语义：收到非法码 ≠ "要求换到 N"，而是"这条指令不可解释"。
         * 执行侧应保持当前角度、只上报，绝不能自己动。
         */
        g_status |= STATUS_INVALID_CMD;
      }
      else if ((((g_target_gear == 0U) || (g_target_gear == 1U)) &&
                ((g_shift_inhibit != 0U) ||
                 (g_vehicle_speed > SPEED_STANDSTILL_KMH))) ||
               ((g_target_gear == 0U) && (g_brake_pct < PARK_BRAKE_MIN_PCT)))
      {
        /*
         * P/R 的拦截条件，任一成立就不执行、保持当前角度：
         *   ① g_shift_inhibit：对端在行驶中重启过，它发的 P/R 不可信
         *   ② 车速 > 5km/h：车还在动就不许挂 P（驻车锁会打齿）、不许挂 R
         *      —— 这一条不依赖对端是否正确，执行侧自己就能判
         *   ③ 挂 P 但刹车 < 30%：没踩住就挂 P，车会溜
         * 注意这里是"拒绝换挡"，不是"切动力"：让车继续按原挡位跑，
         * 比强行挂 P 安全得多。
         */
      }
      else
      {
        lastGear      = g_target_gear;
        g_servo_angle = GearToAngle(lastGear);
        __HAL_TIM_SET_COMPARE(&htim3, SERVO_TIM_CHANNEL, ServoAngleToPulse(g_servo_angle));
        g_actual_gear = lastGear;
      }
    }

    osDelay(SERVO_TASK_PERIOD_MS);
  }
}

/**
  * @brief  CAN 发送任务：10ms 一帧 0x280，顺带喂看门狗
  * @note   B0=实际档 B1=舵机角度 B2=状态 B3=生命计数
  */
void vTask_CAN_Tx(void *argument)
{
  FDCAN_TxHeaderTypeDef TxHeader = {0};
  uint8_t TxData[8];
  uint8_t lifeCounter = 0;

  (void)argument;

  TxHeader.Identifier           = CAN_ID_STATUS;
  TxHeader.IdType               = FDCAN_STANDARD_ID;
  TxHeader.TxFrameType          = FDCAN_DATA_FRAME;
  TxHeader.DataLength           = FDCAN_DLC_BYTES_8;
  TxHeader.ErrorStateIndicator  = FDCAN_ESI_ACTIVE;
  TxHeader.BitRateSwitch        = FDCAN_BRS_OFF;
  TxHeader.FDFormat             = FDCAN_CLASSIC_CAN;
  TxHeader.TxEventFifoControl   = FDCAN_NO_TX_EVENTS;
  TxHeader.MessageMarker        = 0;

  memset(TxData, 0, sizeof(TxData));

  for (;;)
  {
    TxData[0] = g_actual_gear;
    TxData[1] = g_servo_angle;
    TxData[2] = g_status;
    TxData[3] = lifeCounter++;

    /* 发送失败不做重试：10ms 后自然会有下一帧 */
    (void)HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan2, &TxHeader, TxData);

    HAL_IWDG_Refresh(&hiwdg);

    osDelay(CAN_TX_PERIOD_MS);
  }
}

/**
  * @brief  调试串口打印（USART1，115200）
  */
void vTask_Debug(void *argument)
{
  (void)argument;

  for (;;)
  {
    printf("Target:%d Actual:%d Angle:%d Status:%d Spd:%d Inh:%d Cmd:%d Brk:%d Mot:%d BO:%d\r\n",
           g_target_gear, g_actual_gear, g_servo_angle, g_status,
           g_vehicle_speed, g_shift_inhibit, g_motor_cmd, g_brake_pct,
           g_motor_throttle, (int)g_can_busoff_cnt);
    osDelay(DEBUG_PERIOD_MS);
  }
}

/**
  * @brief  板载蓝灯心跳
  */
void vTask_LED(void *argument)
{
  (void)argument;

  for (;;)
  {
    HAL_GPIO_TogglePin(BLUE_GPIO_Port, BLUE_Pin);
    osDelay(LED_PERIOD_MS);
  }
}
/* USER CODE END Application */

