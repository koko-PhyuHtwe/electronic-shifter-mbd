#include "sbw_hmi.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "peripherals_lpuart_1.h"
#include "lpuart_driver.h"
#include "interrupt_manager.h"

/* =========================================================
 *  Ring buffer for received bytes (ISR writes, task reads)
 * ========================================================= */
static volatile uint8_t  s_ringBuf[HMI_RING_BUF_SIZE];
static volatile uint16_t s_ringHead = 0U;
static volatile uint16_t s_ringTail = 0U;

/* =========================================================
 *  Frame parsing state machine
 * ========================================================= */
typedef enum {
    HMI_STATE_DATA = 0,
    HMI_STATE_FF1,
    HMI_STATE_FF2
} HMI_ParseState_t;

static uint8_t        s_frameBuf[HMI_FRAME_MAX_LEN];
static uint8_t        s_frameIndex = 0U;
static HMI_ParseState_t s_parseState = HMI_STATE_DATA;

static uint8_t        s_rxByte = 0U;

/* =========================================================
 *  Local helpers
 * ========================================================= */
static void HMI_RingBufPush(uint8_t data)
{
    uint16_t next = (uint16_t)((s_ringHead + 1U) % HMI_RING_BUF_SIZE);
    if (next != s_ringTail)
    {
        s_ringBuf[s_ringHead] = data;
        s_ringHead = next;
    }
}

static uint8_t HMI_RingBufPop(uint8_t *data)
{
    if (s_ringHead == s_ringTail)
    {
        return 0U;
    }
    *data = s_ringBuf[s_ringTail];
    s_ringTail = (uint16_t)((s_ringTail + 1U) % HMI_RING_BUF_SIZE);
    return 1U;
}

/* =========================================================
 *  LPUART RX callback: runs in ISR context, must be short
 * ========================================================= */
static void HMI_RxCallback(void *driverState, uart_event_t event, void *userData)
{
    (void)driverState;
    (void)userData;

    /* For single-byte reception, RX_FULL and END_TRANSFER fire in sequence.
     * Only push the byte on END_TRANSFER to avoid duplicates. */
    if (event == UART_EVENT_END_TRANSFER)
    {
        HMI_RingBufPush(s_rxByte);
    }

    /* Restart 1-byte reception for next character. */
    (void)LPUART_DRV_ReceiveData(INST_LPUART_1, (uint8_t *)&s_rxByte, 1U);
}

/* =========================================================
 *  Blocking send helpers
 * ========================================================= */
static void HMI_SendByte(uint8_t data)
{
    (void)LPUART_DRV_SendDataBlocking(INST_LPUART_1, &data, 1U, 100U);
}

static void HMI_SendBytes(const uint8_t *data, uint16_t len)
{
    if ((data != NULL) && (len > 0U))
    {
        (void)LPUART_DRV_SendDataBlocking(INST_LPUART_1, data, len, 100U);
    }
}

static void HMI_SendStringRaw(const char *str)
{
    if (str != NULL)
    {
        HMI_SendBytes((const uint8_t *)str, (uint16_t)strlen(str));
    }
}

/* =========================================================
 *  Public protocol functions
 * ========================================================= */
void HMI_Init(void)
{
    /* Set LPUART1 interrupt priority to FreeRTOS safe level (1)
     * before the driver enables the interrupt. */
    INT_SYS_SetPriority(LPUART1_RxTx_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);

    (void)LPUART_DRV_Init(INST_LPUART_1, &lpUartState1, &lpuart_1_InitConfig0);

    /* Install RX callback and start continuous 1-byte reception. */
    (void)LPUART_DRV_InstallRxCallback(INST_LPUART_1, HMI_RxCallback, NULL);
    (void)LPUART_DRV_ReceiveData(INST_LPUART_1, (uint8_t *)&s_rxByte, 1U);
}

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

/* 发送纯文本（不带 Nextion 协议 0xFF 结尾） */
void HMI_SendText(const char *text)
{
    if (text != NULL)
    {
        HMI_SendBytes((const uint8_t *)text, (uint16_t)strlen(text));
    }
}

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
                        s_frameIndex = 0U;
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
