/**
 * @file    sbw_buttons.h
 * @brief   SBW 按键输入 —— P 档键 + 解锁键
 *
 * @details 硬件连接（S32K144 EVB，LQFP100）：
 *            PTB4（脚 28） → P 档按键     p_button
 *            PTB2（脚 48） → 解锁按键     unlock_button
 *
 *          两个键的有效电平**不同**，必须分别配置，不能共用一个宏：
 *
 *            PB2 解锁键：按键一端接 3.3 V，板上另有外部下拉电阻。
 *                        实测松手 0.76 V（外部下拉与片内上拉分压），按下 3.3 V
 *                        → 高电平有效。
 *                        片内上拉已改为下拉，把 0.76 V 压到接近 0 V，
 *                        噪声余量从 0.23 V 提到约 1 V。
 *
 *            PB4 P 档键：实测松开 3.3 V、按下 0 V → 低电平有效（内部上拉）。
 *
 * @note    摇杆两轴由 ADC 模块负责（见 sbw_adc.h），不在本模块。
 */

#ifndef SBW_BUTTONS_H
#define SBW_BUTTONS_H

#include <stdbool.h>

#include "BoardDefines.h"

/* ---------- 1. 引脚分配 ---------- */

#define BTN_P_GPIO              PTB
#define BTN_P_PIN               ( 4U )

#define BTN_UNLOCK_GPIO         PTB
#define BTN_UNLOCK_PIN          ( 2U )

/* ---------- 2. 电平与消抖 ---------- */

/* 1 = 高电平按下，0 = 低电平按下（实测结果，见文件头说明） */
#define BTN_P_ACTIVE_LEVEL      ( 0U )
#define BTN_UNLOCK_ACTIVE_LEVEL ( 1U )

/* 连续读到「按下」多少次才确认。Buttons_Update() 每 10 ms 调一次 → 3 次 = 30 ms */
#define BTN_DEBOUNCE_CNT        ( 3U )

/* ---------- 3. 对外信号 ---------- */

extern volatile bool g_p_button;        /* P 档按键，true = 按下 */
extern volatile bool g_unlock_button;   /* 解锁按键，true = 按下 */

/* ---------- 4. 对外接口 ---------- */

/**
 * @brief  每 10 ms 调用一次：消抖并刷新上面两个信号
 */
void Buttons_Update(void);

#endif /* SBW_BUTTONS_H */