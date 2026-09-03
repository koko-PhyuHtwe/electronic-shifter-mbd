#include "sbw_buttons.h"

volatile uint8_t g_stick_x       = 0U;
volatile bool    g_p_button      = false;
volatile bool    g_unlock_button = false;

/* 每个键的消抖计数 */
static uint8_t s_debounce[4] = {0U, 0U, 0U, 0U};

#define BTN_DEBOUNCE_CNT   3U   /* 连读 3 次(30ms)才确认按下 */

static bool readPin(GPIO_Type *base, uint8_t pin)
{
    uint32_t v = PINS_DRV_ReadPins(base);

    if (BTN_PRESSED_LEVEL == 1U)
    {
        return ((v >> pin) & 1UL) != 0UL;   /* 按下高电平 */
    }
    else
    {
        return ((v >> pin) & 1UL) == 0UL;   /* 按下低电平 */
    }
}

static bool debounced(GPIO_Type *base, uint8_t pin, uint8_t idx)
{
    if (readPin(base, pin))
    {
        if (s_debounce[idx] < 255U)
        {
            s_debounce[idx]++;
        }
    }
    else
    {
        s_debounce[idx] = 0U;
    }
    return (s_debounce[idx] >= BTN_DEBOUNCE_CNT);
}

void Buttons_Update(void)
{
    /* 键1降档 / 键2升档 互斥 */
    if (debounced(BTN1_GPIO, BTN1_PIN, 0U))
    {
        g_stick_x = 1U;                     /* 降档 */
    }
    else if (debounced(BTN2_GPIO, BTN2_PIN, 1U))
    {
        g_stick_x = 2U;                     /* 升档 */
    }
    else
    {
        g_stick_x = 0U;
    }

    g_p_button      = debounced(BTN3_GPIO, BTN3_PIN, 2U);
    g_unlock_button = debounced(BTN4_GPIO, BTN4_PIN, 3U);
}