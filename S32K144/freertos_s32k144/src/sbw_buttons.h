#ifndef SBW_BUTTONS_H
#define SBW_BUTTONS_H

#include "BoardDefines.h"   /* 引入 PTC/PTB GPIO 宏与 PINS_DRV */

/*
 * 5 个按键 → 换挡输入信号（引脚由用户已在 Pins 配置）：
 *   键1 = PTC12  → 降档  (stick_x = 1)
 *   键2 = PTC13  → 升档  (stick_x = 2)
 *   键3 = PTB2   → P 档  (p_button)
 *   键4 = PTB3   → 解锁  (unlock_button)
 *   键5 = PTC14  → 进 M  (stick_y = 2；回 D 暂未接按键)
 * 如需改按键功能，只需改 Buttons_Update() 里的映射。
 */

#define BTN1_GPIO   PTC
#define BTN1_PIN    12U
#define BTN2_GPIO   PTC
#define BTN2_PIN    13U
#define BTN3_GPIO   PTB
#define BTN3_PIN    2U
#define BTN4_GPIO   PTB
#define BTN4_PIN    3U
#define BTN5_GPIO   PTC
#define BTN5_PIN    14U

/* 按键按下电平：1 = 按下为高电平；若你的按键按下接 GND(低电平)，改成 0U */
#define BTN_PRESSED_LEVEL   1U

/* 输出信号（供换挡决策 Stateflow 读取） */
extern volatile uint8_t g_stick_x;         /* 0=中位, 1=降档, 2=升档 */
extern volatile uint8_t g_stick_y;         /* 0=中位, 2=进M；回D(1)暂未接按键 */
extern volatile bool    g_p_button;        /* P 档按键 */
extern volatile bool    g_unlock_button;   /* 解锁按键 */

/* 每 10ms 调用一次，内部做按键消抖并更新输出信号 */
void Buttons_Update(void);

#endif /* SBW_BUTTONS_H */