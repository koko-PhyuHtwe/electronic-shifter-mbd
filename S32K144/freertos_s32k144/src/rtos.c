/**
 * @file    rtos.c
 * @brief   S32K144 采集决策侧 —— 任务框架与整机装配
 *
 * @details 本文件是「上电之后发生了什么」的唯一入口，包含三部分：
 *            1. 硬件初始化 prvSetupHardware()：时钟 → 引脚 → CAN → ADC → 车辆模型 → 串口屏
 *            2. 七个周期任务：ADC 采样、换挡决策、CAN 收发、按键、心跳、调试输出
 *            3. 演示输出 Demo_Update()：把状态机的判断过程翻译成屏幕上的两行文字
 *
 *          硬件映射：
 *            ADC   PTA6 = 刹车    PTA7 = 油门    PTC2 = 摇杆 X    PTC3 = 摇杆 Y
 *            GPIO  PTB4 = P 键（低有效）        PTB2 = 解锁键（高有效，片内下拉）
 *            CAN   FlexCAN2，500 kbps，PTC16 = RX / PTC17 = TX
 *            UART  LPUART1，115200，PTC6 = RX / PTC7 = TX（接串口屏）
 *
 *          任务表（周期固定，优先级数字越大越高）：
 *            vTask_Heartbeat    500ms  prio 2  心跳 + 喂狗（检查五个任务是否都在跑）
 *            vTask_ADC_Sample    10ms  prio 3  采四路 ADC + 每 100ms 推进车辆模型
 *            vTask_Gear_Logic    10ms  prio 3  把输入映射进 Stateflow，跑一步
 *            vTask_CAN_Tx        10ms  prio 3  下发 0x180
 *            vTask_CAN_Rx        10ms  prio 4  收 0x280，维护通信有效标志与超时
 *            vTask_Buttons       10ms  prio 2  读两个按键并消抖
 *            vTask_Debug        100ms  prio 2  刷串口屏 + 事件行
 *
 *          ⚠️ 看门狗刻意不依赖「有没有收到 CAN」——断线是通信故障，不是死机，
 *             断线时系统必须活着并进 FAULT，而不是复位循环。
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#ifndef pdMS_TO_TICKS
    #define pdMS_TO_TICKS( xTimeInMs )    ( ( TickType_t ) ( ( ( TickType_t ) ( xTimeInMs ) * ( TickType_t ) configTICK_RATE_HZ ) / ( TickType_t ) 1000U ) )
#endif

/* SDK includes. */
#include "interrupt_manager.h"
#include "sdk_project_config.h"

#include "BoardDefines.h"
#include "sbw_can.h"
#include "sbw_hmi.h"
#include "sbw_adc.h"
#include "sbw_veh.h"
#include "sbw_buttons.h"
#include "sbw_wdog.h"
#include "GearShift_Stateflow.h"

/* ---------- 1. 对外全局变量 ---------- */

/* 换挡决策输出 (P=0,R=1,N=2,D=3,M1~6=11~16,FAULT=255), used by CAN_Tx / debug */
volatile uint8_t g_current_gear = 0U;

/* 通信有效标志 (收到 0x280 → 1; 超时 → 0), feeds Stateflow::valid */
volatile uint8_t g_comm_valid  = 0U;

/* G474 回传的实际档位 */
volatile uint8_t g_actual_gear = 0U;

/* G474 回传的执行侧状态位（bit0=通信超时 bit1=拒绝执行 P/R） */
volatile uint8_t g_exec_status = 0U;

/* ---------- 2. 配置开关与外设参数 ---------- */

/* 看门狗开关：1 = 启用，0 = 屏蔽（调试期间怕误复位时临时关掉） */
#define WDOG_ENABLE   0U     /* 0 = 不武装看门狗（排查复位用），1 = 正常启用 */

/* 挂 P 挡的刹车门槛：低于这个力度按 P 键无效 */
#define SBW_PARK_BRAKE_MIN   30U

/* 任务活体计数：每个周期任务每跑一圈 +1。
 * 心跳任务每 500ms 比对一次，五个任务全在推进才喂狗。
 * 有任何一个卡死 → 不喂 → 1s 后硬件复位。
 * 注意：这里刻意不检查"有没有收到 CAN"，断线不等于死机。 */
static volatile uint32_t s_aliveAdc  = 0U;
static volatile uint32_t s_aliveGear = 0U;
static volatile uint32_t s_aliveTx   = 0U;
static volatile uint32_t s_aliveRx   = 0U;
static volatile uint32_t s_aliveBtn  = 0U;

/* ---------- 3. 任务参数 ---------- */

/* 任务优先级（数字越大越高） */
#define TASK_HEARTBEAT_PRIORITY   ( tskIDLE_PRIORITY + 2 )
#define TASK_ADC_PRIORITY         ( tskIDLE_PRIORITY + 3 )
#define TASK_GEAR_PRIORITY        ( tskIDLE_PRIORITY + 3 )
#define TASK_CAN_TX_PRIORITY      ( tskIDLE_PRIORITY + 3 )
#define TASK_CAN_RX_PRIORITY      ( tskIDLE_PRIORITY + 4 )
#define TASK_BUTTONS_PRIORITY     ( tskIDLE_PRIORITY + 2 )
#define TASK_DEBUG_PRIORITY       ( tskIDLE_PRIORITY + 2 )

/* 任务周期 [ms] */
#define TASK_HEARTBEAT_PERIOD_MS   ( 500U )
#define TASK_ADC_PERIOD_MS         (  10U )
#define TASK_GEAR_PERIOD_MS        (  10U )
#define TASK_CAN_TX_PERIOD_MS      (  10U )
#define TASK_BUTTONS_PERIOD_MS     (  10U )
#define TASK_DEBUG_PERIOD_MS       ( 100U )

/* ---------- 4. 内部函数声明 ---------- */

static void prvSetupHardware( void );

static void vTask_Heartbeat( void *pvParameters );
static void vTask_ADC_Sample( void *pvParameters );
static void vTask_Gear_Logic( void *pvParameters );
static void vTask_CAN_Tx( void *pvParameters );
static void vTask_CAN_Rx( void *pvParameters );
static void vTask_Buttons( void *pvParameters );
static void vTask_Debug( void *pvParameters );

/* ---------- 5. 启动与初始化 ---------- */

/**
 * @brief  整机启动：建任务 → 交给调度器
 * @note   返回前必须先完成硬件初始化，否则任务一起来就会读到野值
 */
void rtos_start( void )
{
    /* 第一件事：把上一次复位是谁干的记下来（后面要清标志，所以必须最早读） */
    Wdog_CaptureResetCause();

    prvSetupHardware();

    /* Initialize the Stateflow gear-shift state machine (上电固定进 P). */
    GearShift_Stateflow_initialize();

    /* Create SBW application tasks.
     * Debug 任务要跑 snprintf + HMI 组帧，原 256 word(1KB) 不够，
     * 溢出会被 FreeRTOS 抓到并停在这里 —— 而看门狗随后就会把它复位掉，
     * 表现成"一直在复位"。这里把常用任务的栈都加到 384~512 word。 */
    xTaskCreate( vTask_Heartbeat,   "Heart",  256, NULL, TASK_HEARTBEAT_PRIORITY, NULL );
    xTaskCreate( vTask_ADC_Sample,  "ADC",    384, NULL, TASK_ADC_PRIORITY,       NULL );
    xTaskCreate( vTask_Gear_Logic,  "Gear",   512, NULL, TASK_GEAR_PRIORITY,      NULL );
    xTaskCreate( vTask_CAN_Tx,      "CANTx",  384, NULL, TASK_CAN_TX_PRIORITY,    NULL );
    xTaskCreate( vTask_CAN_Rx,      "CANRx",  256, NULL, TASK_CAN_RX_PRIORITY,    NULL );
    xTaskCreate( vTask_Buttons,     "Btns",   256, NULL, TASK_BUTTONS_PRIORITY,   NULL );
    xTaskCreate( vTask_Debug,       "Debug",  512, NULL, TASK_DEBUG_PRIORITY,     NULL );

    vTaskStartScheduler();

    /* Should never reach here. */
    for( ;; );
}

/*-----------------------------------------------------------*/

/**
 * @brief  外设初始化：时钟 → 引脚 → CAN → ADC → 车辆模型 → 串口屏 → 看门狗
 * @note   顺序有讲究：CAN 必须在 ADC 之前（CAN 初始化失败会点灯提示），
 *         HMI 最后（它要开中断），看门狗放最末（前面前全部跑通才开始计时）
 */
static void prvSetupHardware( void )
{
    /* Initialize and configure clocks. */
    CLOCK_SYS_Init(g_clockManConfigsArr, CLOCK_MANAGER_CONFIG_CNT,
                   g_clockManCallbacksArr, CLOCK_MANAGER_CALLBACK_CNT);
    CLOCK_SYS_UpdateConfiguration(0U, CLOCK_MANAGER_POLICY_AGREEMENT);

    /* Initialize all pins (LEDs, CAN2, UART, ADC, buttons). */
    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS0, g_pin_mux_InitConfigArr0);

    /* Initialize FlexCAN2 for SBW communication (interrupt-driven RX). */
    CAN_Init();

    /* Initialize ADC0 (brake, throttle, stick_x, stick_y) + auto calibration. */
    ADC_Init();

    /* Initialize placeholder vehicle/engine model. */
    VehModel_Init();

    /* Initialize LPUART1 and HMI protocol for serial screen. */
    HMI_Init();

    /* 最后一步才武装看门狗：前面所有初始化（含 ADC 自校准）都不占超时窗口。
     * 武装之后由 vTask_Heartbeat 每 500ms 喂一次。 */
    #if (WDOG_ENABLE != 0U)
    Wdog_Init();
    #endif
}

/*-----------------------------------------------------------*/

/**
 * @brief  心跳任务：翻转 LED；启用看门狗时检查五个周期任务是否都在推进
 * @note   只有五个活体计数全部变化才喂狗。这样任何任务卡死都会在 1s 内被复位，
 *         而单纯的 CAN 断线不会（CANTx/CANRx 任务照常跑）
 */
static void vTask_Heartbeat( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_HEARTBEAT_PERIOD_MS );
    uint32_t lastAdc  = 0U;
    uint32_t lastGear = 0U;
    uint32_t lastTx   = 0U;
    uint32_t lastRx   = 0U;
    uint32_t lastBtn  = 0U;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* 心跳的唯一职责：确认五个任务都在跑，然后才喂狗。
         * 任何一个卡住 → 五个条件不全满足 → 不喂 → 1s 后复位。 */
        if( (s_aliveAdc  != lastAdc)  && (s_aliveGear != lastGear) &&
            (s_aliveTx   != lastTx)   && (s_aliveRx   != lastRx)   &&
            (s_aliveBtn  != lastBtn) )
        {
            lastAdc  = s_aliveAdc;
            lastGear = s_aliveGear;
            lastTx   = s_aliveTx;
            lastRx   = s_aliveRx;
            lastBtn  = s_aliveBtn;

            #if (WDOG_ENABLE != 0U)
            Wdog_Feed();
            #endif
        }

        PINS_DRV_TogglePins(LED_BLUE_GPIO, (1 << LED_BLUE));
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief  ADC 采样任务：四路通道 + 每 100ms 推进一次车辆模型
 * @note   摇杆前 1 秒用于采集回中基准，期间强制判中位
 */
static void vTask_ADC_Sample( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_ADC_PERIOD_MS );
    uint32_t tickCount = 0U;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        s_aliveAdc++;

        /* Sample all 4 ADC channels every 10ms.
         * 内含摇杆上电校准（前 1s 采样回中基准，期间摇杆判中位）。 */
        ADC_SampleAll();

        /* Vehicle model: update every 100ms (10 calls × 10ms). */
        tickCount++;
        if( tickCount >= 10U )
        {
            tickCount = 0U;
            VehModel_Update((uint8_t)g_current_gear);
        }
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief  换挡决策任务：把八个输入映射进 Stateflow 结构体，跑一步，取回档位
 * @note   状态机每 10ms 跑一次，与 ADC 同周期，保证摇杆变化能立刻被看到
 */
static void vTask_Gear_Logic( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_GEAR_PERIOD_MS );

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        s_aliveGear++;

        /* Map physical inputs → Stateflow ExtU. */
        GearShift_Stateflow_U.stick_x        = (uint8_T)g_stick_x;
        GearShift_Stateflow_U.stick_y        = (uint8_T)g_stick_y;
        GearShift_Stateflow_U.brake_pedal    = (uint16_T)g_brake_pct;
        GearShift_Stateflow_U.vehicle_speed  = (uint16_T)g_vehicle_speed;
        GearShift_Stateflow_U.engine_rpm     = (uint16_T)g_engine_rpm;
        GearShift_Stateflow_U.throttle_pct   = (uint8_T)((g_throttle_pct > 100U) ? 100U
                                                                                 : g_throttle_pct);
        /* P 键原样透传：模型 v1.24 起，D/M→P 自带
         * `brake_pedal > 30 && throttle_pct == 0 && vehicle_speed == 0`，
         * 即"踩油门或车在走时按 P 无效"已经在模型里。这里不再重复屏蔽，
         * 保持"一条规则只有一个出处"，避免模型和代码两份真相分叉。
         * 执行侧（G474）另有一道独立拦截：车速 > 5 时拒绝任何 P/R 指令。 */
        GearShift_Stateflow_U.p_button       = (boolean_T)g_p_button;
        GearShift_Stateflow_U.unlock_button  = (boolean_T)g_unlock_button;
        GearShift_Stateflow_U.valid          = (boolean_T)g_comm_valid;
        GearShift_Stateflow_U.unlock_button  = (boolean_T)g_unlock_button;
        GearShift_Stateflow_U.valid          = (boolean_T)g_comm_valid;

        /* Run one step of the gear state machine. */
        GearShift_Stateflow_step();

        /* Read current gear output. */
        g_current_gear = GearShift_Stateflow_Y.current_gear;
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief  CAN 发送任务：每 10ms 下发一帧 0x180
 * @note   负载布局见 sbw_can.h：B0/B1 档位、B3 生命计数、B4 车速、B5 油门、B6 刹车
 */
static void vTask_CAN_Tx( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_CAN_TX_PERIOD_MS );
    uint8_t txData[8];
    uint8_t life = 0U;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        s_aliveTx++;

        /* Pack 0x180: B0=target_gear, B1=display, B3=life_counter, B4=vehicle_speed, B5=motor% */
        /* B4 给执行侧用：G474 靠它判断"车还在动时不许进 P/R" */
        txData[CAN_TX_IDX_GEAR]    = (uint8_t)g_current_gear;
        txData[CAN_TX_IDX_DISPLAY] = (uint8_t)g_current_gear;
        txData[2]                  = 0U;
        txData[CAN_TX_IDX_LIFE]    = life++;
        txData[CAN_TX_IDX_SPEED]   = (uint8_t)((g_vehicle_speed > 240U) ? 240U : g_vehicle_speed);
        /* B5 = 电机油门 %：直接把油门踏板百分比发给执行侧驱动风机电机 */
        txData[CAN_TX_IDX_MOTOR]   = (uint8_t)((g_throttle_pct > 100U) ? 100U : g_throttle_pct);
        txData[CAN_TX_IDX_BRAKE]   = (uint8_t)((g_brake_pct > 100U) ? 100U : g_brake_pct);
        txData[7]                  = 0U;

        CAN_SendMessage(CAN_TX_ID_SBW, txData, 8U);
    }
}

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/
/*  通信超时开关（调试/演示用）                                 */
/*  0 = 屏蔽：永按通信有效处理，不会进 FAULT                    */
/*  1 = 开启：500ms 收不到 0x280 判通信失效                     */
/*-----------------------------------------------------------*/
#define COMM_TIMEOUT_ENABLE    1U

/**
 * @brief  CAN 接收任务：从队列取 0x280，维护通信有效标志与超时计数
 * @note   500ms 收不到任何一帧 → g_comm_valid=0 → 状态机进 FAULT
 *         超时门槛由 COMM_TIMEOUT_ENABLE 控制，调试时可整体关掉
 */
static void vTask_CAN_Rx( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( 10U );
    can_frame_t rxFrame;
    static uint32_t timeoutCnt = 0U;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        s_aliveRx++;

        #if (COMM_TIMEOUT_ENABLE != 0U)
        /* Communication timeout: increment every 10ms, cap at 50 (>500ms → FAULT). */
        timeoutCnt++;
        if( timeoutCnt > 50U )
        {
            g_comm_valid = 0U;
            timeoutCnt = 50U;
        }
#else
        /* 超时已屏蔽：始终认为通信有效 */
        (void)timeoutCnt;
        g_comm_valid = 1U;
#endif

        /* 注意：不在这里做 bus-off 重初始化。
         * S32K144 FlexCAN 的 CTRL1[BOFFREC] 默认为 0（自动恢复），bus-off 后硬件
         * 会自动回到总线并清零 TEC，软件不需要插手；而 CAN_Deinit()/CAN_Init()
         * 与发送任务并发执行有竞态（会拆掉正在被 FLEXCAN_DRV_Send 使用的驱动状态），
         * 反而可能把控制器弄成永久的坏状态。 */

        /* Process all frames in the interrupt-driven RX queue. */
        while( CAN_RxAvailable() != 0U )
        {
            CAN_GetRxFrame(&rxFrame);

            if( rxFrame.id == CAN_RX_ID_SBW )
            {
                g_actual_gear = rxFrame.data[CAN_RX_IDX_GEAR];   /* B0 = actual_gear */
                g_exec_status = rxFrame.data[CAN_RX_IDX_STATUS]; /* B2 = 执行侧状态位 */
                g_comm_valid  = 1U;
                timeoutCnt    = 0U;
            }
        }
    }
}

/*-----------------------------------------------------------*/

/**
 * @brief  按键任务：读取 P 键与解锁键（各自的有效电平不同）
 */
static void vTask_Buttons( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_BUTTONS_PERIOD_MS );

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        s_aliveBtn++;

        /* Read P button + unlock button with debounce. */
        Buttons_Update();
    }
}

/*-----------------------------------------------------------*/
/*  演示面板（面试演示用）                                     */
/*                                                            */
/*  只占用 2 个文本控件：                                       */
/*    t0 = 大号档位   P / R / N / D / M1..M6 / FAULT           */
/*    t1 = 状态行：优先显示"为什么换不了挡"，其次档位跳变事件    */
/*         NEED UNLOCK / NEED BRAKE / STOP FIRST / CAN LOST     */
/*         RPM LOW / SPEED LOW / SPEED HIGH                     */
/*    t2 = 操作提示   （默认关，DEMO_SHOW_HINT 改 1 才用）       */
/*                                                            */
/*  换挡条件不另占控件——屏幕上的数字就是条件：                 */
/*    n0 刹车%   n1 转速   n2 车速   n3 摇杆X   n4 摇杆Y       */
/*    n5 = 解锁|P键 编码（1=解锁按下  2=P按下  3=都按）        */
/*-----------------------------------------------------------*/

/* ---------- 7. 演示输出（串口屏两行状态） ---------- */

#define DEMO_LINE_LEN     24U
#define DEMO_SHOW_HINT     0   /* 0 = 不显示 t2 提示行（只用 t0/t1） */

/* 摇杆排查模式：置 1 时 n3/n4 显示摇杆原始 ADC 值（0~4095 缩小到 0~255）。 */
#define STICK_DEBUG_RAW    0

static char    s_event[DEMO_LINE_LEN];
static uint8_t s_lastGear   = 0U;
static uint8_t s_eventHold  = 0U;   /* 事件保持时间（单位 100ms），10 = 1s */

/* 阴影缓存：值没变就不发串口，避免每 100ms 刷满屏幕带宽 */
static char s_shadow[3][DEMO_LINE_LEN];

static const char *Demo_GearName(uint8_t g)
{
    switch (g)
    {
        case 0U:  return "P";
        case 1U:  return "R";
        case 2U:  return "N";
        case 3U:  return "D";
        case 11U: return "M1";
        case 12U: return "M2";
        case 13U: return "M3";
        case 14U: return "M4";
        case 15U: return "M5";
        case 16U: return "M6";
        default:  return "FAULT";
    }
}

static void Demo_SetEvent(const char *msg)
{
    (void)strncpy(s_event, msg, DEMO_LINE_LEN - 1U);
    s_event[DEMO_LINE_LEN - 1U] = '\0';
}

static void Demo_SendChanged(const char *widget, const char *value, uint8_t slot)
{
    if (strcmp(s_shadow[slot], value) != 0)
    {
        HMI_SendString(widget, value);
        (void)strncpy(s_shadow[slot], value, DEMO_LINE_LEN - 1U);
        s_shadow[slot][DEMO_LINE_LEN - 1U] = '\0';
    }
}

/* M 挡升挡最低车速（与生成码里 speed>=25/50/80/120/160 一致） */
static const uint16_t kMUpSpeed[6]   = { 25U, 50U, 80U, 120U, 160U, 0U };
/* M 挡降挡最高车速（与生成码里 speed<=15/40/70/100/140 一致） */
static const uint16_t kMDownSpeed[6] = { 0U, 15U, 40U, 70U, 100U, 140U };

/* 为什么现在换不了挡？没有问题返回 NULL。 */
static const char *Demo_Reason(uint8_t gear)
{
    uint8_t  unlock = (g_unlock_button != 0U) ? 1U : 0U;
    uint8_t  brake  = (uint8_t)g_brake_pct;
    uint16_t speed  = (uint16_t)g_vehicle_speed;
    uint16_t rpm    = (uint16_t)g_engine_rpm;
    uint8_t  idx;

    /* ---- 最高优先级：通信 / 故障 ---- */
    if (g_comm_valid == 0U)
    {
        /* 收不到 0x280 时，用 ESR1 的错误类型 + 错误计数判断到底是哪一层断了：
         *   NO ACK   ：帧完整发出去了，但总线上没人给应答 → 对面不在总线上
         *              （没上电 / 没跑 / CANH·CANL 断 / 收发器没电）
         *   TX BITER ：我方发出去的位和回读不一致 → 本板收发器或引脚坏了
         *   FORM ERR ：能收发但解不出帧 → 波特率不一致 / 终端电阻 / 强干扰
         *   BUSOFF   ：已经被踢出总线
         *   T = 发送错误计数，涨到 128 以上即"错误被动"（屏幕上的 BUS ERR） */
        static char s_busMsg[DEMO_LINE_LEN];
        uint8_t  tec  = 0U;
        uint8_t  rec  = 0U;
        uint16_t fl   = 0U;
        uint8_t  st   = 0U;
        const char *why;

        CAN_GetErrCounters(&tec, &rec);
        fl = CAN_GetErrFlags();
        st = CAN_GetBusState();

        if (st == 3U)
        {
            return "CAN INIT FAIL";      /* 控制器没初始化成功 */
        }
        if ((fl & 0x30U) != 0U)
        {
            why = "TX BITER";            /* BIT0ERR | BIT1ERR：本板发送通道 */
        }
        else if ((fl & 0x08U) != 0U)
        {
            why = "NO ACK";              /* ACKERR：帧好的，就是没人应答 */
        }
        else if ((fl & 0x17U) != 0U)
        {
            why = "FORM ERR";            /* STF / FRM / CRC：格式类 */
        }
        else if (st == 2U)
        {
            why = "BUSOFF";
        }
        else if ((tec > 0U) && (rec == 0U))
        {
            /* 自己在发、发出去的帧在报错，却一帧都没收到：
             * 对面根本没在应答（没上电/没跑/线断/收发器没电）。
             * 比 "SILENT" 明确得多 —— TEC 涨说明 ACK 错误在累积。 */
            why = "NO PEER";
        }
        else
        {
            why = "SILENT";              /* 没收到帧，也没发过错：对端静默 */
        }

        (void)snprintf(s_busMsg, sizeof(s_busMsg), "%s T%u R%u", why, tec, rec);
        return s_busMsg;
    }
    /* FAULT 是锁死状态，唯一出口是"人工确认"：按 P 键回 P 挡。
     * 出不去的时候，屏幕要直接告诉人卡在哪一条，否则只能干瞪眼。 */
    if (gear == 255U)
    {
        if (g_comm_valid == 0U)          { return "FAULT: NO LINK"; }
        if (g_p_button == 0U)            { return "FAULT: PRESS P"; }
        if (speed >= 5U)                 { return "FAULT: STOP CAR"; }
        if (g_stick_x != 0U)             { return "FAULT: CENTER"; }
        return "FAULT: RECOVERING";
    }
    if ((g_exec_status & CAN_STATUS_SHIFT_REFUSED) != 0U) { return "EXEC LOCK"; }
    if ((g_exec_status & CAN_STATUS_MOTOR_CUT) != 0U)     { return "PWR CUT"; }
    /* 刹车优先：刹车和油门同时踩下时，动力必须让位（只在踩油门时提示，平时不占行） */
    if (((g_exec_status & CAN_STATUS_BRAKE_CUT) != 0U) && (g_throttle_pct > 0U))
    {
        return "BRAKE CUT";
    }
    /* 挡位不给动力（P/N/R）：只有在踩油门时才提示，平时不占用状态行 */
    if (((g_exec_status & CAN_STATUS_GEAR_INHIBIT) != 0U) && (g_throttle_pct > 0U))
    {
        return "NO DRIVE";
    }

    /* ---- 摇杆回中基准未建立：上电 1s 内，或者一直在复位 ----
     * 正常情况开机 1 秒后就会消失。如果这条一直挂着，说明板子
     * 在反复重启（看门狗喂不上 / 硬件复位），校准永远做不完，
     * 摇杆就会一直被判成中位。 */
    if (g_stick_rest_x == 0U) { return "STICK CAL"; }

    /* ---- P 档：解锁 → 刹车 → 停车，三道门逐级提示 ---- */
    if (gear == 0U)
    {
        if (unlock == 0U) { return "NEED UNLOCK"; }
        if (brake < 30U)  { return "NEED BRAKE"; }
        if (speed >= 5U)  { return "STOP FIRST"; }
        return "PUSH FWD/BACK";
    }

    /* ---- 任何档位按 P 键，都要先停车；D/M 里只要还在给油也一律拒绝 ---- */
    if (g_p_button != 0U)
    {
        if (brake < SBW_PARK_BRAKE_MIN) { return "NEED BRAKE"; }
        if (speed >= 5U) { return "STOP FIRST"; }

        if (((gear == 3U) || ((gear >= 11U) && (gear <= 16U))) &&
            ((g_throttle_pct > 0U) || (g_vehicle_speed > 0U)))
        {
            return "STOP FIRST";
        }
    }

    /* ---- N→R / R→N 需要停车 ---- */
    if ((gear == 2U) && (g_stick_x == 2U) && (speed >= 5U)) { return "STOP FIRST"; }
    if ((gear == 1U) && (g_stick_x == 1U) && (speed >= 5U)) { return "STOP FIRST"; }

    /* ---- M 挡内的升降挡门槛 ---- */
    if ((gear >= 11U) && (gear <= 16U))
    {
        idx = (uint8_t)(gear - 11U);
        if (g_stick_x == 2U)                            /* 想升挡 */
        {
            if (rpm < 2000U) { return "RPM LOW"; }
            if ((idx < 5U) && (speed < kMUpSpeed[idx])) { return "SPEED LOW"; }
        }
        else if (g_stick_x == 1U)                       /* 想降挡 */
        {
            if ((idx > 0U) && (speed > kMDownSpeed[idx])) { return "SPEED HIGH"; }
        }
    }

    return NULL;
}

static void Demo_Update(void)
{
    char        buf[DEMO_LINE_LEN];
#if STICK_DEBUG_RAW
    char        dbg[32];            /* 排查模式下的长行，比 t1 显示区更宽松 */
#endif
    uint8_t     gear = (uint8_t)g_current_gear;
    const char *reason;
    const char *line;
#if (DEMO_SHOW_HINT == 1)
    const char *hint;
#endif

    /* ---- 1. 档位跳变 → 事件行（保持 1 秒） ---- */
    if (gear != s_lastGear)
    {
        (void)snprintf(buf, sizeof(buf), "%s>%s",
                       Demo_GearName(s_lastGear), Demo_GearName(gear));
        Demo_SetEvent(buf);
        s_lastGear = gear;
        s_eventHold = 10U;          /* 10 × 100ms = 1s */
    }

    if (s_event[0] == '\0')
    {
        Demo_SetEvent("SYSTEM READY");
    }

    /* ---- 2. 决定 t1：事件优先 1 秒，之后让位给"为什么换不了挡" ---- */
    reason = Demo_Reason(gear);
    if (s_eventHold > 0U)
    {
        s_eventHold--;
        line = s_event;
    }
    else if (reason != NULL)
    {
        line = reason;
    }
    else
    {
        line = s_event;
    }

#if STICK_DEBUG_RAW
    /* 排查模式：t1 改成"复位原因 + 校准状态"。
     *   WDOG X0 Y0 r0    → 上一次是看门狗复位（心跳没喂上/某任务卡死）
     *   LOCKUP ...       → CPU 锁死（HardFault），多半是栈溢出/野指针
     *   POR   X183 Y183  → 正常上电，两轴都校好了（183×16≈2928）
     *   X 或 Y 显示 0    → 该轴还没校完；r 在涨说明采到的基准不合格在重采 */
    {
        const char *cause;

        if ((g_reset_cause & RCM_SRS_LOCKUP_MASK) != 0U)   { cause = "LOCKUP"; }
        else if ((g_reset_cause & RCM_SRS_WDOG_MASK) != 0U){ cause = "WDOG"; }
        else if ((g_reset_cause & RCM_SRS_PIN_MASK) != 0U) { cause = "PIN"; }
        else if ((g_reset_cause & RCM_SRS_SW_MASK) != 0U)  { cause = "SW"; }
        else if ((g_reset_cause & RCM_SRS_POR_MASK) != 0U) { cause = "POR"; }
        else                                               { cause = "?"; }

        (void)snprintf(dbg, sizeof(dbg), "%s X%u Y%u r%u",
                       cause,
                       (unsigned)(g_stick_rest_x / 16U),
                       (unsigned)(g_stick_rest_y / 16U),
                       (unsigned)(g_stick_cal_rej & 0xFFU));
        line = dbg;
    }
#endif

    /* ---- 3. 推送 t0 / t1 ---- */
    Demo_SendChanged("moudle.t0.txt", Demo_GearName(gear), 0U);
    Demo_SendChanged("moudle.t1.txt", line, 1U);

#if (DEMO_SHOW_HINT == 1)
    /* ---- 4. 可选提示行 t2 ---- */
    switch (gear)
    {
        case 0U:  hint = "UNLOCK+BRAKE -> GO"; break;
        case 1U:  hint = "FWD -> N (STOP)";    break;
        case 2U:  hint = "FWD->D  BACK->R";    break;
        case 3U:  hint = "BACK->N  Y-R = M";   break;
        case 11U: case 12U: case 13U:
        case 14U: case 15U: case 16U:
                  hint = "BACK=UP FWD=DOWN";   break;
        default:  hint = "STOP + PRESS P";     break;
    }
    Demo_SendChanged("moudle.t2.txt", hint, 2U);
#endif
}

/*-----------------------------------------------------------*/
/*  HMI 输出                                                   */
/*   n0(%)   = 刹车 %            n1(rpm) = 转速                */
/*   n2(km/h)= 车速              n3(x)   = stick_x 0/1/2       */
/*   n4(y)   = stick_y 0/1/2     n5      = 解锁+P 键编码       */
/*   b0 = 返回键（保留）   t0=档位  t1=事件  t2=提示(可选)     */
/*-----------------------------------------------------------*/

/**
 * @brief  调试输出任务：刷新屏幕上的六个数值 + 两行状态
 * @note   n0=刹车% n1=转速 n2=车速 n3=摇杆X n4=摇杆Y n5=解锁|P键
 *         t0=大档位 t1=事件或状态行 t2=可选提示行
 */
static void vTask_Debug( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_DEBUG_PERIOD_MS );
    char rxFrame[HMI_FRAME_MAX_LEN];

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* ---- 原始数值（标定/排查用） ---- */
        HMI_SendNumber("moudle.n0.val", (int32_t)g_brake_pct);       /* 刹车 %      */
        HMI_SendNumber("moudle.n1.val", (int32_t)g_engine_rpm);      /* 转速 rpm    */
        HMI_SendNumber("moudle.n2.val", (int32_t)g_vehicle_speed);   /* 车速 km/h   */
#if STICK_DEBUG_RAW
        /* 排查模式：n3/n4 直接显示摇杆原始值（0~4095 缩到 0~255）。
         * 回中应该在 183 左右，推到底到 0 或 255 附近。
         * 如果一直是 0 / 一直是 255 不动 → 电位器没供电或接线断了。 */
        HMI_SendNumber("moudle.n3.val", (int32_t)(g_stick_x_raw / 16U));
        HMI_SendNumber("moudle.n4.val", (int32_t)(g_stick_y_raw / 16U));
#else
        HMI_SendNumber("moudle.n3.val", (int32_t)g_stick_x);         /* 摇杆X 0/1/2 */
        HMI_SendNumber("moudle.n4.val", (int32_t)g_stick_y);         /* 摇杆Y 0/1/2 */
#endif
        /* n5 编码 = unlock | (p_button<<1)
         *   0=都不按  1=只按解锁  2=只按P  3=两个都按 */
        HMI_SendNumber("moudle.n5.val",
                       (int32_t)((g_unlock_button ? 1 : 0) | (g_p_button ? 2 : 0)));

        /* ---- 演示面板：大档位 / 条件清单 / 提示 / 事件日志 ---- */
        Demo_Update();

        /* Process incoming frames from the serial screen. */
        while( HMI_GetFrame(rxFrame, sizeof(rxFrame)) != 0U )
        {
            /* TODO: parse touch events or commands from HMI. */
            (void)rxFrame;
        }
    }
}

/*-----------------------------------------------------------*/
/* FreeRTOS hooks */
/*-----------------------------------------------------------*/

/* ---------- 8. FreeRTOS 钩子 ---------- */

/**
 * @brief  堆分配失败钩子
 * @note   任务创建失败会走到这里，停住便于调试器看是哪个任务
 */
void vApplicationMallocFailedHook( void )
{
    PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));
    taskDISABLE_INTERRUPTS();
    for( ;; );
}

/**
 * @brief  栈溢出钩子
 * @details 故意做成「可见」而不是静默死循环：
 *            喂一次狗 + LED 快闪 + 原地循环
 *          这样表现为「屏幕冻结 + LED 快闪、但系统不复位」，
 *          与真正的看门狗超时（整机复位）区分得开。
 * @note   排查栈溢出那次就是靠这个把「复位循环」和「看门狗配置错」分开的
 */
void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
    volatile uint32_t i;
    uint32_t blink = 0U;

    ( void ) pcTaskName;
    ( void ) pxTask;

    taskDISABLE_INTERRUPTS();

    /* 原地停住但不让看门狗复位：一边喂狗一边快闪 LED。
     * 现象 = 屏幕数字冻结 + LED 快闪。
     * 这样能区分两种完全不同的故障：
     *   灯闪 + 不复位   → 任务栈不够（就是这里）
     *   一直复位，灯不闪 → 心跳没喂上 / 别的任务卡死 */
    for( ;; )
    {
        Wdog_Feed();
        blink = (blink == 0U) ? 1U : 0U;
        if( blink != 0U )
        {
            PINS_DRV_SetPins( LED_GPIO, ( 1 << LED1 ) );
        }
        else
        {
            PINS_DRV_ClearPins( LED_GPIO, ( 1 << LED1 ) );
        }
        for( i = 0U; i < 300000U; i++ )
        {
            /* busy wait */
        }
    }
}

/**
 * @brief  空闲钩子（当前为空）
 */
void vApplicationIdleHook( void )
{
    volatile size_t xFreeHeapSpace;
    xFreeHeapSpace = xPortGetFreeHeapSize();
    if( xFreeHeapSpace > 100 )
    {
        /* configTOTAL_HEAP_SIZE can be reduced. */
    }
}

void vMainConfigureTimerForRunTimeStats( void ) {}
unsigned long ulMainGetRunTimeCounterValue( void ) { return 0UL; }
void vApplicationTickHook( void ) {}