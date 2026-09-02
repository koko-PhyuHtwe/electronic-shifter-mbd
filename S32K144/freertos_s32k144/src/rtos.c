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
static void vTask_Unlock_Button( void *pvParameters );
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

	/* Create SBW application tasks. */
	xTaskCreate( vTask_Heartbeat,   "Heart", 256, NULL, TASK_HEARTBEAT_PRIORITY,   NULL );
	xTaskCreate( vTask_ADC_Sample,  "ADC",   256, NULL, TASK_ADC_PRIORITY,         NULL );
//	xTaskCreate( vTask_Gear_Logic,  "Gear",  configMINIMAL_STACK_SIZE, NULL, TASK_GEAR_PRIORITY,        NULL );
//	xTaskCreate( vTask_CAN_Tx,      "CANTx", configMINIMAL_STACK_SIZE, NULL, TASK_CAN_TX_PRIORITY,      NULL );
	xTaskCreate( vTask_CAN_Rx,      "CANRx", configMINIMAL_STACK_SIZE, NULL, TASK_CAN_RX_PRIORITY,      NULL );
	xTaskCreate( vTask_Unlock_Button,"Unlock",256, NULL, TASK_UNLOCK_BTN_PRIORITY,  NULL );
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

        /* 每 100ms 把刹车/转速/车速发到串口屏，并更新占位车辆模型。 */
        printCnt++;
        if( printCnt >= 10U )
        {
            printCnt = 0U;

            /* 占位车辆模型：油门→转速、油门/刹车→车速 */
            VehModel_Update();

            HMI_SendNumber("moudle.n0.val", (int32_t)g_brake_pct);     /* 刹车 %   */
            HMI_SendNumber("moudle.n1.val", (int32_t)g_engine_rpm);    /* 转速 rpm */
            HMI_SendNumber("moudle.n2.val", (int32_t)g_vehicle_speed); /* 车速 km/h */
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

        /* TODO: call GearShift_Stateflow_step() and get current_gear. */
    }
}
/*-----------------------------------------------------------*/

static void vTask_CAN_Tx( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_CAN_TX_PERIOD_MS );
    uint8_t txData[8];
    uint8_t txCounter = 0U;
    uint8_t i;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* Send a test frame every 100 ms (10 task periods). */
        txCounter++;
        if( txCounter >= 10U )
        {
            txData[0] = 0xAAU;
            txData[1] = txCounter;
            for( i = 2U; i < 8U; i++ )
            {
                txData[i] = (uint8_t)(0x10U + i);
            }

            CAN_SendMessage(CAN_TX_ID_SBW, txData, 8U);

            txCounter = 0U;
        }
    }
}
/*-----------------------------------------------------------*/

static void vTask_CAN_Rx( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( 10U );
    can_frame_t rxFrame;
    static uint32_t rxCnt = 0U;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        /* Process all frames available in the interrupt-driven Rx queue. */
        while( CAN_RxAvailable() != 0U )
        {
            CAN_GetRxFrame(&rxFrame);

            if( rxFrame.id == CAN_RX_ID_SBW )
            {
                rxCnt++;
                /* 每收到 20 帧 0x280 翻一次绿灯，便于观察频率 */
                if( (rxCnt % 20U) == 0U )
                {
                    PINS_DRV_TogglePins(LED_GPIO, (1 << LED1));
                }
            }
        }
    }
}
/*-----------------------------------------------------------*/

static void vTask_Unlock_Button( void *pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xPeriod = pdMS_TO_TICKS( TASK_UNLOCK_BTN_PERIOD_MS );
    uint8_t debounceCnt = 0U;
    uint8_t sent = 0U;
    uint32_t pinVal;
    uint8_t pressed;

    (void)pvParameters;
    xNextWakeTime = xTaskGetTickCount();

    for( ;; )
    {
        vTaskDelayUntil( &xNextWakeTime, xPeriod );

        pinVal  = PINS_DRV_ReadPins(UNLOCK_BTN_GPIO);
        pressed = (uint8_t)((pinVal & (1UL << UNLOCK_BTN)) != 0UL);

        if( pressed != 0U )
        {
            debounceCnt++;
            if( (debounceCnt >= 3U) && (sent == 0U) )
            {
                /* 熄灭故障灯（低电平点亮，高电平熄灭） */
                PINS_DRV_SetPins(FAULT_LED_GPIO, (1UL << FAULT_LED));
                HMI_SendText("解锁成功\r\n");
                sent = 1U;
            }
        }
        else
        {
            debounceCnt = 0U;
            sent        = 0U;
        }
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
