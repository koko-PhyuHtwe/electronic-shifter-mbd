/**
 * @file    BoardDefines.h
 * @brief   S32K144 EVB 板级定义 —— 只放 LED 相关的引脚宏
 *
 * 引脚编号来自 S32DS Pin Mux 生成的 board/pin_mux.c：
 *   PTD16（脚 21） LED_1_B  蓝灯 —— 心跳任务 500ms 翻转
 *   PTD15（脚 22） LED_2_G  绿灯 —— 故障/异常指示
 *
 * 按键引脚在 sbw_buttons.h，模拟通道在 sbw_adc.h，不要往这里堆。
 */

#ifndef BOARD_DEFINES_H
#define BOARD_DEFINES_H

#include <stdint.h>
#include <stdbool.h>

#include "pins_driver.h"

/* ---------------- LED ---------------- */
#define LED_GPIO            PTD

/* 蓝灯：心跳 */
#define LED_BLUE            16U
#define LED_BLUE_GPIO       PTD

/* 绿灯：故障/异常指示（沿用旧名字 LED1，sbw_can.c 里也在用） */
#define LED1                15U

#endif /* BOARD_DEFINES_H */