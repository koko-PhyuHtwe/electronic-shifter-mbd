/***********************************************************************************************************************
 * @file    sbw_can.c
 * @brief   SBW 项目 FlexCAN2 通信实现
 * @details 中断接收（MB1，过滤 0x280） + 阻塞发送（MB0，ID 0x180）
 **********************************************************************************************************************/

#include "sbw_can.h"
#include "FreeRTOS.h"
#include "interrupt_manager.h"
#include "BoardDefines.h"
#include "pins_driver.h"

/***********************************************************************************************************************
 * 静态变量
 **********************************************************************************************************************/

/* 发送信息 */
static flexcan_data_info_t s_canTxInfo = {
    .msg_id_type = FLEXCAN_MSG_ID_STD,
    .data_length = 8U,
    .is_remote   = false,
};

/* 接收信息 */
static flexcan_data_info_t s_canRxInfo = {
    .msg_id_type = FLEXCAN_MSG_ID_STD,
    .data_length = 8U,
    .is_remote   = false,
};

/* 驱动内部使用的接收缓冲区 */
static flexcan_msgbuff_t s_canRxBuffer;

/* 接收环形队列 */
static can_frame_t s_canRxQueue[CAN_RX_QUEUE_SIZE];
static volatile uint8_t s_canRxHead = 0U;
static volatile uint8_t s_canRxTail = 0U;
static uint8_t s_canInitialized = 0U;

/* 调试计数 */
volatile uint32_t g_canTxCount = 0U;
volatile uint32_t g_canTxFailCount = 0U;
volatile uint32_t g_canRxCount = 0U;

/***********************************************************************************************************************
 * 静态函数声明
 **********************************************************************************************************************/

static void CAN_Callback(uint8_t instance,
                         flexcan_event_type_t eventType,
                         uint32_t buffIdx,
                         flexcan_state_t *flexcanState);

/***********************************************************************************************************************
 * API 函数实现
 **********************************************************************************************************************/

/* 初始化 FlexCAN2 */
void CAN_Init(void)
{
    status_t status;

    if (s_canInitialized != 0U)
    {
        return;
    }

    /* 初始化 FlexCAN2 模块（内部使能 NVIC 中断，使用默认优先级） */
    status = FLEXCAN_DRV_Init(CAN_INSTANCE, &flexcanState2, &flexcanInitConfig2);
    if (status != STATUS_SUCCESS)
    {
        /* Initialization failed - turn on green LED as error indicator. */
        PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));
        return;
    }

    /* 配置发送邮箱 M0 */
    status = FLEXCAN_DRV_ConfigTxMb(CAN_INSTANCE, CAN_TX_MB_IDX, &s_canTxInfo, CAN_TX_ID_SBW);
    if (status != STATUS_SUCCESS)
    {
        PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));
        return;
    }

    /* 配置接收邮箱 M1，过滤 0x280 */
    status = FLEXCAN_DRV_ConfigRxMb(CAN_INSTANCE, CAN_RX_MB_IDX, &s_canRxInfo, CAN_RX_ID_SBW);
    if (status != STATUS_SUCCESS)
    {
        PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));
        return;
    }

    /* 安装事件回调 */
    FLEXCAN_DRV_InstallEventCallback(CAN_INSTANCE, CAN_Callback, NULL);

    /* 启动中断接收 */
    status = FLEXCAN_DRV_Receive(CAN_INSTANCE, CAN_RX_MB_IDX, &s_canRxBuffer);
    if (status != STATUS_SUCCESS)
    {
        PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));
        return;
    }

    s_canInitialized = 1U;
}

/* 反初始化 FlexCAN2 */
void CAN_Deinit(void)
{
    if (s_canInitialized != 0U)
    {
        FLEXCAN_DRV_Deinit(CAN_INSTANCE);
        s_canInitialized = 0U;
    }
}

/* 发送一帧 CAN 消息（阻塞式） */
void CAN_SendMessage(uint32_t id, const uint8_t *data, uint8_t len)
{
    status_t status;

    if (s_canInitialized == 0U)
    {
        return;
    }

    if ((len == 0U) || (len > 8U))
    {
        return;
    }

    s_canTxInfo.data_length = len;
    status = FLEXCAN_DRV_Send(CAN_INSTANCE,
                              CAN_TX_MB_IDX,
                              &s_canTxInfo,
                              id,
                              data);
    if (status == STATUS_SUCCESS)
    {
        g_canTxCount++;
    }
    else
    {
        g_canTxFailCount++;
    }
}

/* 查询接收队列是否有数据 */
uint8_t CAN_RxAvailable(void)
{
    return (s_canRxHead != s_canRxTail) ? 1U : 0U;
}

/* 从接收队列读取一帧数据 */
void CAN_GetRxFrame(can_frame_t *frame)
{
    if (frame == NULL)
    {
        return;
    }

    if (CAN_RxAvailable() != 0U)
    {
        *frame = s_canRxQueue[s_canRxTail];
        s_canRxTail = (uint8_t)((s_canRxTail + 1U) % CAN_RX_QUEUE_SIZE);
    }
    else
    {
        frame->id = 0U;
        frame->dlc = 0U;
        memset(frame->data, 0U, 8U);
    }
}

/***********************************************************************************************************************
 * 中断回调函数
 **********************************************************************************************************************/

static void CAN_Callback(uint8_t instance,
                         flexcan_event_type_t eventType,
                         uint32_t buffIdx,
                         flexcan_state_t *flexcanState)
{
    (void)instance;
    (void)flexcanState;

    if ((eventType == FLEXCAN_EVENT_RX_COMPLETE) && (buffIdx == CAN_RX_MB_IDX))
    {
        uint8_t next = (uint8_t)((s_canRxHead + 1U) % CAN_RX_QUEUE_SIZE);

        if (next != s_canRxTail)
        {
            s_canRxQueue[s_canRxHead].id  = s_canRxBuffer.msgId;
            s_canRxQueue[s_canRxHead].dlc = s_canRxBuffer.dataLen;
            memcpy(s_canRxQueue[s_canRxHead].data, s_canRxBuffer.data, 8U);
            s_canRxHead = next;
            g_canRxCount++;
        }
        /* 队列满则丢弃该帧 */

        /* 重新启动接收，准备接收下一帧 */
        FLEXCAN_DRV_Receive(CAN_INSTANCE, CAN_RX_MB_IDX, &s_canRxBuffer);
    }
}
