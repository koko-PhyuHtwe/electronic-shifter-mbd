/**
 * @file    sbw_wdog.h
 * @brief   S32K144 独立看门狗（WDOG）封装
 *
 * @details 触发条件：心跳任务每 500 ms 检查 5 个业务任务的活体计数，
 *          全部推进才喂一次狗，超时 1.0 s → 正常余量 2 倍。
 *
 *          时钟：LPO 128 kHz（见 clock_config.c）÷ 256 = 500 Hz，计数 500 次 = 1.0 s。
 *
 * @note    喂狗链路**不依赖 CAN**。断线时 ADC / Gear / Tx / Rx / Buttons
 *          五个任务照常运行、计数照常推进，狗照常喂——
 *          断线只会让系统进 FAULT，不会引发复位循环。
 *
 * @warning 任务栈不足会先触发 FreeRTOS 的栈溢出钩子，表现为「屏幕冻结 + 灯快闪」；
 *          不要把它和看门狗超时（复位）混淆，两者的处置方式完全不同。
 */

#ifndef SBW_WDOG_H
#define SBW_WDOG_H

#include <stdint.h>

/* ---------- 1. 常量 ---------- */

/* 超时时间 [ms]：1.0 s，心跳 500 ms 喂一次 */
#define WDOG_TIMEOUT_MS     ( 1000U )

/* ---------- 2. 对外信号 ---------- */

/* 上一次复位的原因位（RCM_SRS 原值），0 表示尚未采集 */
extern volatile uint32_t g_reset_cause;

/* ---------- 3. 对外接口 ---------- */

/**
 * @brief  武装看门狗，写完立即开始计数（上电初始化最后一步，只调用一次）
 */
void Wdog_Init(void);

/**
 * @brief  喂狗
 * @note   只能由心跳任务在确认所有业务任务存活后调用
 */
void Wdog_Feed(void);

/**
 * @brief  上电第一件事调用：读走 RCM_SRS 并清标志
 * @note   用来区分看门狗复位 / CPU 锁死 / 外部复位 / 正常上电
 */
void Wdog_CaptureResetCause(void);

#endif /* SBW_WDOG_H */