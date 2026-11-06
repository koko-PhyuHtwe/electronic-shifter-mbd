/**
 * @file    sbw_can.c
 * @brief   FlexCAN2 通信实现 —— 中断接收 + 阻塞发送 + 总线诊断
 *
 * @details 接收：邮箱 M1 过滤 0x280，中断里把帧搬进 16 槽环形队列，队列满则丢弃。
 *          发送：邮箱 M0 发 0x180，FLEXCAN_DRV_Send 是非阻塞的（内部
 *                StartSendData(..., false)），失败只返回状态码，不会把任务卡住——
 *                这一点很关键：CAN 断线时发送任务照常跑，看门狗照常喂。
 *
 *          总线诊断三个接口是把 ESR1 拆开给人看：
 *            CAN_GetBusState()    节点处于什么状态
 *            CAN_GetErrCounters() 发送/接收错误计数
 *            CAN_GetErrFlags()    最后一次错误是什么类型
 *          三者结合可以把「线的问题」「对面不说话」「我方收发器坏」区分开。
 */

#include "sbw_can.h"

#include <stddef.h>
#include <string.h>

#include "FreeRTOS.h"
#include "BoardDefines.h"
#include "interrupt_manager.h"
#include "pins_driver.h"

/* ---------- 1. 私有变量 ---------- */

/* 发送信息模板（data_length 每次发送前按实际长度改写） */
static flexcan_data_info_t s_canTxInfo =
{
    .msg_id_type = FLEXCAN_MSG_ID_STD,
    .data_length = 8U,
    .is_remote   = false,
};

/* 接收信息模板 */
static flexcan_data_info_t s_canRxInfo =
{
    .msg_id_type = FLEXCAN_MSG_ID_STD,
    .data_length = 8U,
    .is_remote   = false,
};

/* 驱动内部接收缓冲：中断里把这一帧搬进环形队列后重新挂起接收 */
static flexcan_msgbuff_t s_canRxBuffer;

/* 环形队列（单生产者中断 / 单消费者任务，无需临界区） */
static can_frame_t       s_canRxQueue[CAN_RX_QUEUE_SIZE];
static volatile uint8_t  s_canRxHead     = 0U;
static volatile uint8_t  s_canRxTail     = 0U;
static uint8_t           s_canInitialized = 0U;

/* ---------- 2. 对外信号 ---------- */

volatile uint32_t g_canTxCount     = 0U;
volatile uint32_t g_canTxFailCount = 0U;
volatile uint32_t g_canRxCount     = 0U;

/* ---------- 3. 私有函数 ---------- */

static void CAN_Callback(uint8_t instance,
                         flexcan_event_type_t eventType,
                         uint32_t buffIdx,
                         flexcan_state_t *flexcanState);

/* ---------- 4. 对外接口 ---------- */

/**
 * @brief  初始化 FlexCAN2
 * @note   任何一步失败都点亮绿灯后返回，不死等——上层会看到「CAN INIT FAIL」并降级
 */
void CAN_Init(void)
{
    status_t status;

    if (s_canInitialized != 0U)
    {
        return;
    }

    /* 初始化控制器（内部使能 NVIC 中断） */
    status = FLEXCAN_DRV_Init(CAN_INSTANCE, &flexcanState2, &flexcanInitConfig2);
    if (status != STATUS_SUCCESS)
    {
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

    /* 配置接收邮箱 M1，只收 0x280 */
    status = FLEXCAN_DRV_ConfigRxMb(CAN_INSTANCE, CAN_RX_MB_IDX, &s_canRxInfo, CAN_RX_ID_SBW);
    if (status != STATUS_SUCCESS)
    {
        PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));
        return;
    }

    FLEXCAN_DRV_InstallEventCallback(CAN_INSTANCE, CAN_Callback, NULL);

    /* 挂起接收：之后每收一帧由回调重新挂起 */
    status = FLEXCAN_DRV_Receive(CAN_INSTANCE, CAN_RX_MB_IDX, &s_canRxBuffer);
    if (status != STATUS_SUCCESS)
    {
        PINS_DRV_ClearPins(LED_GPIO, (1 << LED1));
        return;
    }

    s_canInitialized = 1U;
}

/**
 * @brief  反初始化 FlexCAN2
 */
void CAN_Deinit(void)
{
    if (s_canInitialized != 0U)
    {
        FLEXCAN_DRV_Deinit(CAN_INSTANCE);
        s_canInitialized = 0U;
    }
}

/**
 * @brief  发送一帧标准帧
 * @param  id   帧 ID
 * @param  data 负载
 * @param  len  负载长度 1~8
 */
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
    status = FLEXCAN_DRV_Send(CAN_INSTANCE, CAN_TX_MB_IDX, &s_canTxInfo, id, data);

    if (status == STATUS_SUCCESS)
    {
        g_canTxCount++;
    }
    else
    {
        g_canTxFailCount++;
    }
}

/**
 * @brief  接收队列是否有数据
 * @retval 1 = 有，0 = 空
 */
uint8_t CAN_RxAvailable(void)
{
    return (s_canRxHead != s_canRxTail) ? 1U : 0U;
}

/**
 * @brief  从队列取一帧；队列空时返回全 0 帧
 * @param  frame 输出缓冲，NULL 直接返回
 */
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
        frame->id  = 0U;
        frame->dlc = 0U;
        memset(frame->data, 0U, 8U);
    }
}

/**
 * @brief  FlexCAN 事件回调（中断上下文）
 * @note   只做搬运：拷贝进队列 → 重新挂起接收。不做计算、不打印、不阻塞。
 */
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
        /* 队列满：丢弃这一帧。丢帧好过阻塞中断 */

        FLEXCAN_DRV_Receive(CAN_INSTANCE, CAN_RX_MB_IDX, &s_canRxBuffer);
    }
}

/* ---------- 5. 总线诊断 ---------- */

/**
 * @brief  把 ESR1 的 FLTCONF 翻译成「节点状态」
 * @retval 0 = error-active，1 = error-passive，2 = bus-off，3 = 未初始化
 */
uint8_t CAN_GetBusState(void)
{
    uint32_t esr1;
    uint8_t  fltConf;

    if (s_canInitialized == 0U)
    {
        return 3U;
    }

    esr1    = FLEXCAN_DRV_GetErrorStatus(CAN_INSTANCE);
    fltConf = (uint8_t)((esr1 >> 4U) & 0x3U);   /* ESR1[5:4] FLTCONF */

    if ((fltConf & 0x2U) != 0U)
    {
        return 2U;                              /* 1x = bus-off */
    }
    if (fltConf == 0x1U)
    {
        return 1U;                              /* 01 = error-passive */
    }

    return 0U;
}

/**
 * @brief  读取发送 / 接收错误计数器
 * @param  tec 发送错误计数 ESR1[23:16]
 * @param  rec 接收错误计数 ESR1[31:24]
 *
 * @details 这两个数是判断「谁的问题」最直接的证据：
 *            TEC 涨而 REC 恒为 0  → 我方帧没人应答，对面不在总线上
 *            TEC 与 REC 同时涨    → 双方都能听到对方但解不出，波特率或干扰问题
 */
void CAN_GetErrCounters(uint8_t *tec, uint8_t *rec)
{
    uint32_t esr1;

    if ((tec == NULL) || (rec == NULL))
    {
        return;
    }

    if (s_canInitialized == 0U)
    {
        *tec = 0xFFU;
        *rec = 0xFFU;
        return;
    }

    esr1 = FLEXCAN_DRV_GetErrorStatus(CAN_INSTANCE);
    *tec = (uint8_t)((esr1 >> 16U) & 0xFFU);
    *rec = (uint8_t)((esr1 >> 24U) & 0xFFU);
}

/**
 * @brief  读取 ESR1 错误类型锁存位
 * @retval bit0 STFERR / bit1 FRMERR / bit2 CRCERR / bit3 ACKERR / bit4 BIT0ERR / bit5 BIT1ERR
 *
 * @details 这几个位表示「曾经发生过」，用来分清是我方发不出去还是对面不吭声：
 *            ACKERR  帧完整发出去了，但总线上没人给应答
 *            BIT0ERR 我方发出的位和回读不一致 → 收发器 / 引脚问题
 */
uint16_t CAN_GetErrFlags(void)
{
    uint32_t esr1;
    uint16_t flags = 0U;

    if (s_canInitialized == 0U)
    {
        return 0U;
    }

    esr1 = FLEXCAN_DRV_GetErrorStatus(CAN_INSTANCE);

    if ((esr1 & CAN_ESR1_STFERR_MASK)  != 0U) { flags |= 0x01U; }
    if ((esr1 & CAN_ESR1_FRMERR_MASK)  != 0U) { flags |= 0x02U; }
    if ((esr1 & CAN_ESR1_CRCERR_MASK)  != 0U) { flags |= 0x04U; }
    if ((esr1 & CAN_ESR1_ACKERR_MASK)  != 0U) { flags |= 0x08U; }
    if ((esr1 & CAN_ESR1_BIT0ERR_MASK) != 0U) { flags |= 0x10U; }
    if ((esr1 & CAN_ESR1_BIT1ERR_MASK) != 0U) { flags |= 0x20U; }

    return flags;
}