/*
 * Copyright (c) 2015 - 2016 , Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY NXP "AS IS" AND ANY EXPRESSED OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL NXP OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * SBW application task framework for S32K144.
 *
 * This file sets up the FreeRTOS environment for the shift-by-wire project:
 * - Clock initialization via SDK clock manager
 * - GPIO configuration for LEDs and button
 * - Five application tasks running at fixed periods:
 *     vTask_ADC_Sample  : 10 ms
 *     vTask_Gear_Logic  : 10 ms
 *     vTask_CAN_Tx      : 10 ms
 *     vTask_CAN_Rx      : event driven (currently polling)
 *     vTask_Debug       : 100 ms
 */

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
#include "GearShift_Stateflow.h"

/* 换挡决策输出（P=0, R=1, N=2, D=3, FAULT=255），供 CAN_Tx / 调试显示使用 */
volatile uint8_t g_current_gear = 0U;
/* 通信有效标志（收到 0x280=1，超时=0，喂 Stateflow 的 valid） */
volatile uint8_t g_comm_valid  = 0U;
/* G474 回传的实际档位 */
volatile uint8_t g_actual_gear = 0U;



/*-----------------------------------------------------------*/

/*
 * Setup the NVIC, LED outputs, and button inputs.
 */
static void prvSetupHardware( void );

/*
 * SBW application tasks.
 */
static void vTask_Heartbeat( void *pvParameters );
static void vTask_ADC_Sample( void *pvParameters );
static void vTask_Gear_Logic( void *pvParameters );
static void vTask_CAN_Tx( void *pvParameters );
static void vTask_CAN_Rx( void *pvParameters );
static void vTask_Buttons( void *pvParameters );
static void vTask_Debug( void *pvParameters );

/*-----------------------------------------------------------*/

/* Task priorities. */
#define TASK_HEARTBEAT_PRIORITY   ( tskIDLE_PRIORITY + 2 )
#define TASK_ADC_PRIORITY         ( tskIDLE_PRIORITY + 3 )
#define TASK_GEAR_PRIORITY        ( tskIDLE_PRIORITY + 3 )
#define TASK_CAN_TX_PRIORITY      ( tskIDLE_PRIORITY + 3 )
#define TASK_CAN_RX_PRIORITY      ( tskIDLE_PRIORITY + 4 )
#define TASK_UNLOCK_BTN_PRIORITY  ( tskIDLE_PRIORITY + 2 )
#define TASK_DEBUG_PRIORITY       ( tskIDLE_PRIORITY + 2 )

/* Task periods [ms]. */
#define TASK_HEARTBEAT_PERIOD_MS   ( 500U )
#define TASK_ADC_PERIOD_MS         ( 10U )
#define TASK_GEAR_PERIOD_MS        ( 10U )
#define TASK_CAN_TX_PERIOD_MS      ( 10U )
#define TASK_UNLOCK_BTN_PERIOD_MS  ( 10U )
#define TASK_DEBUG_PERIOD_MS       ( 100U )

/*-----------------------------------------------------------*/

void rtos_start( void )
{
	/* Configure the NVIC, LED outputs and button inputs. */
	prvSetupHardware();

	/* Initialize the Stateflow gear-shift state machine. */
	GearShift_Stateflow_initialize();

	/* Create SBW application tasks. */
	xTaskCreate( vTask_Heartbeat,   "Heart", 256, NULL, TASK_HEARTBEAT_PRIORITY,   NULL );
	xTaskCreate( vTask_ADC_Sample,  "ADC",   256, NULL, TASK_ADC_PRIORITY,         NULL );
	xTaskCreate( vTask_Gear_Logic,  "Gear",  256, NULL, TASK_GEAR_PRIORITY,        NULL );
	xTaskCreate( vTask_CAN_Tx,      "CANTx", 256, NULL, TASK_CAN_TX_PRIORITY,      NULL );
	xTaskCreate( vTask_CAN_Rx,      "CANRx", configMINIMAL_STACK_SIZE, NULL, TASK_CAN_RX_PRIORITY,      NULL );
	xTaskCreate( vTask_Buttons,     "Btns",  256, NULL, TASK_UNLOCK_BTN_PRIORITY,  NULL );
//	xTaskCreate( vTask_Debug,       "Debug", 256, NULL, TASK_DEBUG_PRIORITY,  NULL );

	/* Start the scheduler. */
	vTaskStartScheduler();

	/* If all is well, the scheduler will now be running, and the following line
	will never be reached.  If the following line does execute, then there was
	insufficient FreeRTOS heap memory available for the idle and/or timer tasks
	to be created.  See the memory management section on the FreeRTOS web site
	for more details. */
	for( ;; );
}
/*-----------------------------------------------------------*/

/* The ISR executed when the user button is pushed. */
void vPort_C_ISRHandler( void )
{
    portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

    /* Clear the interrupt before leaving. */
    PINS_DRV_ClearPortIntFlagCmd(BTN_PORT);

    /* TODO: add button event handling for SBW. */

    portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
}
/*-----------------------------------------------------------*/

static void vTask_Heartbeat( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_HEARTBEAT_PERIOD_MS );

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* Toggle blue LED every 500 ms -> 1 Hz blink. */
        PINS_DRV_TogglePins(LED_BLUE_GPIO, (1 << LED_BLUE));
    }
}
/*-----------------------------------------------------------*/

static void vTask_ADC_Sample( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_ADC_PERIOD_MS );
    uint32_t printCnt = 0U;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* 每 10ms 采样刹车 + 油门两路 ADC。 */
        ADC_SampleAll();

        /* 每 500ms 发一批到串口屏（降频便于肉眼观察；临时验证用） */
        printCnt++;
        if( printCnt >= 50U )
        {
            printCnt = 0U;

            /* 占位车辆模型：油门→转速、油门/刹车→车速 */
            VehModel_Update();

            HMI_SendNumber("moudle.n0.val", (int32_t)g_brake_pct);     /* 刹车 %   */
            HMI_SendNumber("moudle.n1.val", (int32_t)g_engine_rpm);    /* 转速 rpm */
            HMI_SendNumber("moudle.n2.val", (int32_t)g_vehicle_speed); /* 车速 km/h */

            /* 验证按键读取（临时，验证完可删） */
            HMI_SendNumber("moudle.n3.val", (int32_t)g_stick_x);       /* 0中/1降/2升 */
            HMI_SendNumber("moudle.n4.val", (int32_t)g_p_button);      /* P 档按键 0/1 */
            HMI_SendNumber("moudle.n5.val", (int32_t)g_stick_y);       /* 进M按键 0/2（暂顶替解锁键） */

            /* 当前档位字母 → t3.txt（P/R/N/D/M/F） */
            {
                const char *gearStr = "F";
                switch (g_current_gear)
                {
                    case 0U:  gearStr = "P"; break;
                    case 1U:  gearStr = "R"; break;
                    case 2U:  gearStr = "N"; break;
                    case 3U:  gearStr = "D"; break;
                    case 11U:
                    case 12U:
                    case 13U:
                    case 14U:
                    case 15U:
                    case 16U: gearStr = "M"; break;   /* 手动档 */
                    default:  gearStr = "F"; break;   /* 255 = FAULT */
                }
                HMI_SendString("moudle.t3.txt", gearStr);
            }

            /* M 档位数字 → t1.txt（+1 ~ +6） */
            {
                const char *mStr = "+0";
                switch (g_current_gear)
                {
                    case 11U: mStr = "+1"; break;
                    case 12U: mStr = "+2"; break;
                    case 13U: mStr = "+3"; break;
                    case 14U: mStr = "+4"; break;
                    case 15U: mStr = "+5"; break;
                    case 16U: mStr = "+6"; break;
                    default:  mStr = "+0"; break;
                }
                HMI_SendString("moudle.t1.txt", mStr);
            }
        }
    }
}
/*-----------------------------------------------------------*/

static void vTask_Gear_Logic( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_GEAR_PERIOD_MS );

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* 1. 输入映射：采集量/占位量 → Stateflow 输入 */
        GearShift_Stateflow_U.stick_x        = (uint8_T)g_stick_x;
        GearShift_Stateflow_U.stick_y        = (uint8_T)g_stick_y;        /* 进 M(=2) / 回 D(=1) */
        GearShift_Stateflow_U.brake_pedal    = (uint16_T)g_brake_pct;
        GearShift_Stateflow_U.vehicle_speed  = (uint16_T)g_vehicle_speed;
        GearShift_Stateflow_U.engine_rpm     = (uint16_T)g_engine_rpm;
        GearShift_Stateflow_U.p_button       = (boolean_T)g_p_button;
        GearShift_Stateflow_U.unlock_button  = (boolean_T)g_unlock_button;
        GearShift_Stateflow_U.valid          = (boolean_T)g_comm_valid;   /* 收到 0x280 帧有效 */

        /* 2. 跑一帧换挡状态机（x_prev 自锁已在 Stateflow 内部实现，此处直接喂原值） */
        GearShift_Stateflow_step();

        /* 3. 读输出当前档位（后续接 CAN_Tx 发 0x180） */
        g_current_gear = GearShift_Stateflow_Y.current_gear;
    }
}
/*-----------------------------------------------------------*/

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

        /* 打包 0x180：Byte0 目标档, Byte1 显示档, Byte3 生命帧 */
        txData[0] = (uint8_t)g_current_gear;   /* target_gear */
        txData[1] = (uint8_t)g_current_gear;   /* gear_display */
        txData[2] = 0U;                        /* reserved */
        txData[3] = life++;                    /* life_counter */
        txData[4] = 0U;
        txData[5] = 0U;
        txData[6] = 0U;
        txData[7] = 0U;

        CAN_SendMessage(CAN_TX_ID_SBW, txData, 8U);
    }
}
/*-----------------------------------------------------------*/

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

        /* 通信超时检测：每 10ms +1，收到 0x280 清零；>50(=500ms) 判超时 */
        timeoutCnt++;
        if( timeoutCnt > 50U )
        {
            g_comm_valid = 0U;
            timeoutCnt = 50U;
        }

        /* 处理中断驱动的接收队列里的所有帧 */
        while( CAN_RxAvailable() != 0U )
        {
            CAN_GetRxFrame(&rxFrame);

            if( rxFrame.id == CAN_RX_ID_SBW )
            {
                g_actual_gear = rxFrame.data[0];   /* Byte0 = actual_gear */
                g_comm_valid  = 1U;
                timeoutCnt    = 0U;
            }
        }
    }
}
/*-----------------------------------------------------------*/

static void vTask_Buttons( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_UNLOCK_BTN_PERIOD_MS );

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* 每 10ms 读取 4 个按键（消抖），更新 stick_x / p_button / unlock_button */
        Buttons_Update();
    }
}
/*-----------------------------------------------------------*/

static void vTask_Debug( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_DEBUG_PERIOD_MS );
    static uint16_t s_testCounter = 0U;
    char rxFrame[HMI_FRAME_MAX_LEN];

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* Send test data to serial screen every 100 ms. */
        HMI_SendNumber("data.n0.val", (int32_t)s_testCounter);
        HMI_SendFloat("data.x0.val", (float)s_testCounter * 0.01f, 2U);
        s_testCounter++;

        /* Display CAN debug counters on serial screen. */
        HMI_SendNumber("can.tx",      (int32_t)g_canTxCount);
        HMI_SendNumber("can.txfail",  (int32_t)g_canTxFailCount);
        HMI_SendNumber("can.rx",      (int32_t)g_canRxCount);

        /* Process incoming frames from the serial screen. */
        while( HMI_GetFrame(rxFrame, sizeof(rxFrame)) != 0U )
        {
            /* TODO: parse touch events or commands from HMI. */
            (void)rxFrame;
        }
    }
}
/*-----------------------------------------------------------*/

static void prvSetupHardware( void )
{

    /* Initialize and configure clocks
     *  -   Setup system clocks, dividers
     *  -   see clock manager component for more details
     */
    CLOCK_SYS_Init(g_clockManConfigsArr, CLOCK_MANAGER_CONFIG_CNT,
                   g_clockManCallbacksArr, CLOCK_MANAGER_CALLBACK_CNT);
    CLOCK_SYS_UpdateConfiguration(0U, CLOCK_MANAGER_POLICY_AGREEMENT);

    /* Initialize all pins configured by S32 Configuration Tools.
     * This writes the pin mux, direction, pull-up/down settings for
     * LEDs, button and CAN2 (PTC16/PTC17). */
    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS0, g_pin_mux_InitConfigArr0);



	/* Install Button interrupt handler */
    INT_SYS_InstallHandler(BTN_PORT_IRQn, vPort_C_ISRHandler, (isr_t *)NULL);
    /* Enable Button interrupt handler */
    INT_SYS_EnableIRQ(BTN_PORT_IRQn);

    /* The interrupt calls an interrupt safe API function - so its priority must
    be equal to or lower than configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY. */
    INT_SYS_SetPriority( BTN_PORT_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY );

    /* Initialize FlexCAN2 for SBW communication. */
    CAN_Init();

    /* Initialize ADC0 (brake pedal PTA6, throttle PTA7) + auto calibration. */
    ADC_Init();

    /* Initialize placeholder vehicle/engine model. */
    VehModel_Init();

    /* Initialize LPUART1 and HMI protocol for serial screen. */
    HMI_Init();

}
/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */

	/* Turn on green LED as a visual indicator of heap exhaustion. */
	PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));

	taskDISABLE_INTERRUPTS();
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected. */
	taskDISABLE_INTERRUPTS();
	for( ;; );
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
    volatile size_t xFreeHeapSpace;

	/* This function is called on each cycle of the idle task.  In this case it
	does nothing useful, other than report the amount of FreeRTOS heap that
	remains unallocated. */
	xFreeHeapSpace = xPortGetFreeHeapSize();

	if( xFreeHeapSpace > 100 )
	{
		/* By now, the kernel has allocated everything it is going to, so
		if there is a lot of heap remaining unallocated then
		the value of configTOTAL_HEAP_SIZE in FreeRTOSConfig.h can be
		reduced accordingly. */
	}

}
/*-----------------------------------------------------------*/

/* The Blinky build configuration does not include run time stats gathering,
however, the Full and Blinky build configurations share a FreeRTOSConfig.h
file.  Therefore, dummy run time stats functions need to be defined to keep the
linker happy. */
void vMainConfigureTimerForRunTimeStats( void ) {}
unsigned long ulMainGetRunTimeCounterValue( void ) { return 0UL; }

/* A tick hook is used by the "Full" build configuration.  The Full and blinky
build configurations share a FreeRTOSConfig.h header file, so this simple build
configuration also has to define a tick hook - even though it does not actually
use it for anything. */
void vApplicationTickHook( void ) {}

/*-----------------------------------------------------------*/
