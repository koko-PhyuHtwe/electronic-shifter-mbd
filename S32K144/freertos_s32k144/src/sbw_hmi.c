/**
 * @file    sbw_hmi.c
 * @brief   串口屏通信实现 —— Nextion 指令拼接 + 接收帧解析
 *
 * @details 发送：每个指令由「字符串 + 0xFF 0xFF 0xFF」组成。这里逐段阻塞发送
 *          （LPUART_DRV_SendDataBlocking，超时 100ms），因为指令很短且都在
 *          100ms 调试任务里调用，阻塞的代价可以接受，换来的是不需要发送队列。
 *
 *          接收：中断里逐字节入环形缓冲，任务里用三态机解析：
 *                DATA  ——  收到 0xFF 进入 FF1
 *                FF1   ——  再收到 0xFF 进入 FF2；否则把 0xFF 和本字节都当数据
 *                FF2   ——  再收到 0xFF 即一帧结束；否则把两个 0xFF 和本字节当数据
 *          这样即使数据本身含 0xFF 也不会误判帧尾。
 *
 *          ⚠️ 中断里只做「入环形缓冲 + 重新挂接收」，不做解析、不打印、不阻塞。
 */

#include "sbw_hmi.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "peripherals_lpuart_1.h"
#include "lpuart_driver.h"
#include "interrupt_manager.h"

/* ---------- 1. 私有变量 ---------- */

/* 接收环形缓冲：中断写 head，任务写 tail，head==tail 表示空 */
static volatile uint8_t  s_ringBuf[HMI_RING_BUF_SIZE];
static volatile uint16_t s_ringHead = 0U;
static volatile uint16_t s_ringTail = 0U;

/* 帧解析状态机 */
typedef enum
{
    HMI_STATE_DATA = 0,
    HMI_STATE_FF1,
    HMI_STATE_FF2
} HMI_ParseState_t;

static uint8_t          s_frameBuf[HMI_FRAME_MAX_LEN];
static uint8_t          s_frameIndex = 0U;
static HMI_ParseState_t s_parseState = HMI_STATE_DATA;

/* 逐字节接收的落点 */
static uint8_t          s_rxByte = 0U;

/* ---------- 2. 私有函数 ---------- */

static void HMI_RingBufPush(uint8_t data);
static uint8_t HMI_RingBufPop(uint8_t *data);
static void HMI_RxCallback(void *driverState, uart_event_t event, void *userData);
static void HMI_SendByte(uint8_t data);
static void HMI_SendBytes(const uint8_t *data, uint16_t len);
static void HMI_SendStringRaw(const char *str);

/**
 * @brief  向环形缓冲写入一个字节；缓冲满则丢弃
 * @param  data 待写入字节
 */
static void HMI_RingBufPush(uint8_t data)
{
    uint16_t next = (uint16_t)((s_ringHead + 1U) % HMI_RING_BUF_SIZE);

    if (next != s_ringTail)
    {
        s_ringBuf[s_ringHead] = data;
        s_ringHead            = next;
    }
}

/**
 * @brief  从环形缓冲取出一个字节
 * @param  data 输出
 * @retval 1 = 取到，0 = 空
 */
static uint8_t HMI_RingBufPop(uint8_t *data)
{
    if (s_ringHead == s_ringTail)
    {
        return 0U;
    }

    *data      = s_ringBuf[s_ringTail];
    s_ringTail = (uint16_t)((s_ringTail + 1U) % HMI_RING_BUF_SIZE);

    return 1U;
}

/**
 * @brief  LPUART 接收回调（中断上下文）
 * @note   单字节接收会先产生 RX_FULL 再产生 END_TRANSFER，只在后者入队，
 *         否则每个字节会被写两次。函数末尾必须重新挂起接收，否则只收一字节。
 */
static void HMI_RxCallback(void *driverState, uart_event_t event, void *userData)
{
    (void)driverState;
    (void)userData;

    if (event == UART_EVENT_END_TRANSFER)
    {
        HMI_RingBufPush(s_rxByte);
    }

    (void)LPUART_DRV_ReceiveData(INST_LPUART_1, (uint8_t *)&s_rxByte, 1U);
}

/**
 * @brief  阻塞发送一个字节
 */
static void HMI_SendByte(uint8_t data)
{
    (void)LPUART_DRV_SendDataBlocking(INST_LPUART_1, &data, 1U, 100U);
}

/**
 * @brief  阻塞发送一段字节
 */
static void HMI_SendBytes(const uint8_t *data, uint16_t len)
{
    if ((data != NULL) && (len > 0U))
    {
        (void)LPUART_DRV_SendDataBlocking(INST_LPUART_1, data, len, 100U);
    }
}

/**
 * @brief  发送以 '\0' 结尾的字符串（不带结尾符）
 */
static void HMI_SendStringRaw(const char *str)
{
    if (str != NULL)
    {
        HMI_SendBytes((const uint8_t *)str, (uint16_t)strlen(str));
    }
}

/* ---------- 3. 对外接口 ---------- */

/**
 * @brief  初始化 LPUART1 并挂起逐字节接收
 */
void HMI_Init(void)
{
    /* 必须先设优先级、后由驱动打开中断，否则中断里调用 FreeRTOS 会断言 */
    INT_SYS_SetPriority(LPUART1_RxTx_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);

    (void)LPUART_DRV_Init(INST_LPUART_1, &lpUartState1, &lpuart_1_InitConfig0);
    (void)LPUART_DRV_InstallRxCallback(INST_LPUART_1, HMI_RxCallback, NULL);
    (void)LPUART_DRV_ReceiveData(INST_LPUART_1, (uint8_t *)&s_rxByte, 1U);
}

/**
 * @brief  发送字符串指令：<name>="<value>" + 0xFF×3
 */
void HMI_SendString(const char *name, const char *value)
{
    HMI_SendStringRaw(name);
    HMI_SendStringRaw("=\"");
    HMI_SendStringRaw(value);
    HMI_SendStringRaw("\"");
    HMI_SendByte(0xFFU);
    HMI_SendByte(0xFFU);
    HMI_SendByte(0xFFU);
}

/**
 * @brief  发送数值指令：<name>=<value> + 0xFF×3
 */
void HMI_SendNumber(const char *name, int32_t value)
{
    char buf[16];

    (void)snprintf(buf, sizeof(buf), "%ld", (long)value);

    HMI_SendStringRaw(name);
    HMI_SendStringRaw("=");
    HMI_SendStringRaw(buf);
    HMI_SendByte(0xFFU);
    HMI_SendByte(0xFFU);
    HMI_SendByte(0xFFU);
}

/**
 * @brief  发送浮点指令：<name>=<value> + 0xFF×3
 * @note   运行时拼格式串，避免为每个小数位数各写一份代码
 */
void HMI_SendFloat(const char *name, float value, uint8_t decimals)
{
    char buf[24];
    char fmt[8];

    if (decimals > 6U)
    {
        decimals = 6U;
    }

    (void)snprintf(fmt, sizeof(fmt), "%%.%df", decimals);
    (void)snprintf(buf, sizeof(buf), fmt, (double)value);

    HMI_SendStringRaw(name);
    HMI_SendStringRaw("=");
    HMI_SendStringRaw(buf);
    HMI_SendByte(0xFFU);
    HMI_SendByte(0xFFU);
    HMI_SendByte(0xFFU);
}

/**
 * @brief  发送原始文本，不带 0xFF 结尾
 */
void HMI_SendText(const char *text)
{
    if (text != NULL)
    {
        HMI_SendBytes((const uint8_t *)text, (uint16_t)strlen(text));
    }
}

/**
 * @brief  解析出一条完整指令
 * @param  out    输出缓冲
 * @param  outLen 缓冲长度
 * @retval 1 = 取到，0 = 暂无
 */
uint8_t HMI_GetFrame(char *out, uint16_t outLen)
{
    uint8_t byte;

    while (HMI_RingBufPop(&byte) != 0U)
    {
        switch (s_parseState)
        {
            case HMI_STATE_DATA:
                if (byte == 0xFFU)
                {
                    s_parseState = HMI_STATE_FF1;
                }
                else
                {
                    if (s_frameIndex < (HMI_FRAME_MAX_LEN - 1U))
                    {
                        s_frameBuf[s_frameIndex++] = byte;
                    }
                    else
                    {
                        s_frameIndex = 0U;      /* 超长：丢弃整帧重来 */
                    }
                }
                break;

            case HMI_STATE_FF1:
                if (byte == 0xFFU)
                {
                    s_parseState = HMI_STATE_FF2;
                }
                else
                {
                    /* 单个 0xFF 属于数据内容，补回缓冲 */
                    if (s_frameIndex < (HMI_FRAME_MAX_LEN - 1U))
                    {
                        s_frameBuf[s_frameIndex++] = 0xFFU;
                    }
                    if (s_frameIndex < (HMI_FRAME_MAX_LEN - 1U))
                    {
                        s_frameBuf[s_frameIndex++] = byte;
                    }
                    s_parseState = HMI_STATE_DATA;
                }
                break;

            case HMI_STATE_FF2:
                if (byte == 0xFFU)
                {
                    /* 连续三个 0xFF：一帧结束 */
                    s_frameBuf[s_frameIndex] = '\0';

                    if ((out != NULL) && (outLen > 0U))
                    {
                        (void)strncpy(out, (const char *)s_frameBuf, (size_t)(outLen - 1U));
                        out[outLen - 1U] = '\0';
                    }

                    s_frameIndex = 0U;
                    s_parseState = HMI_STATE_DATA;

                    return 1U;
                }
                else
                {
                    /* 只有两个 0xFF，说明是数据内容，补回缓冲 */
                    if (s_frameIndex < (HMI_FRAME_MAX_LEN - 1U))
                    {
                        s_frameBuf[s_frameIndex++] = 0xFFU;
                    }
                    if (s_frameIndex < (HMI_FRAME_MAX_LEN - 1U))
                    {
                        s_frameBuf[s_frameIndex++] = 0xFFU;
                    }
                    if (s_frameIndex < (HMI_FRAME_MAX_LEN - 1U))
                    {
                        s_frameBuf[s_frameIndex++] = byte;
                    }
                    s_parseState = HMI_STATE_DATA;
                }
                break;

            default:
                s_parseState = HMI_STATE_DATA;
                break;
        }
    }

    return 0U;
}