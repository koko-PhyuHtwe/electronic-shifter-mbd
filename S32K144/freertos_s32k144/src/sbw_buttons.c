/**
 * @file    sbw_buttons.c
 * @brief   按键消抖与电平解释
 *
 * @details 只有两个按键：P 档键（PTB4，低有效）与解锁键（PTB2，高有效）。
 *          每个键各自维护一个计数器：连续读到按下才累加，读到松开立即清零，
 *          计数达到 BTN_DEBOUNCE_CNT（30 ms）才认定为按下。
 */

#include "sbw_buttons.h"

/* ---------- 1. 常量 ---------- */

#define BTN_P_IDX           ( 0U )
#define BTN_UNLOCK_IDX      ( 1U )
#define BTN_COUNT           ( 2U )

/* ---------- 2. 对外信号 ---------- */

volatile bool g_p_button      = false;
volatile bool g_unlock_button = false;

/* ---------- 3. 私有变量 ---------- */

static uint8_t s_debounceCnt[BTN_COUNT] = { 0U, 0U };

/* ---------- 4. 私有函数 ---------- */

/**
 * @brief  读单个引脚电平并解释为「是否按下」
 * @param  base        端口基址
 * @param  pin         引脚序号 0~31
 * @param  activeLevel 1 = 高电平按下，0 = 低电平按下
 * @retval true = 按下
 */
static bool BoardKeyIsPressed(GPIO_Type *base, uint8_t pin, uint8_t activeLevel)
{
    uint32_t level  = PINS_DRV_ReadPins(base);
    bool     isHigh = (((level >> pin) & 1UL) != 0UL);

    return (activeLevel != 0U) ? isHigh : (bool)(!isHigh);
}

/**
 * @brief  带消抖的按键读取
 * @param  base        端口基址
 * @param  pin         引脚序号
 * @param  activeLevel 有效电平
 * @param  idx         该键在 s_debounceCnt 中的下标
 * @retval true = 已确认按下（连续 BTN_DEBOUNCE_CNT 次读到按下）
 */
static bool BoardKeyDebounced(GPIO_Type *base, uint8_t pin, uint8_t activeLevel, uint8_t idx)
{
    if (BoardKeyIsPressed(base, pin, activeLevel))
    {
        if (s_debounceCnt[idx] < 255U)
        {
            s_debounceCnt[idx]++;
        }
    }
    else
    {
        s_debounceCnt[idx] = 0U;
    }

    return (s_debounceCnt[idx] >= BTN_DEBOUNCE_CNT);
}

/* ---------- 5. 对外接口 ---------- */

/**
 * @brief  刷新两个按键的消抖状态
 */
void Buttons_Update(void)
{
    g_p_button      = BoardKeyDebounced(BTN_P_GPIO, BTN_P_PIN,
                                        BTN_P_ACTIVE_LEVEL, BTN_P_IDX);
    g_unlock_button = BoardKeyDebounced(BTN_UNLOCK_GPIO, BTN_UNLOCK_PIN,
                                        BTN_UNLOCK_ACTIVE_LEVEL, BTN_UNLOCK_IDX);
}