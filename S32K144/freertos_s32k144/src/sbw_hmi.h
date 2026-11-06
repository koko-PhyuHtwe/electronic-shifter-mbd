/**
 * @file    sbw_hmi.h
 * @brief   串口屏（Nextion 指令集）通信接口
 *
 * @details 物理层：LPUART1，115200 8N1，TX = PTC7 / RX = PTC6。
 *          屏幕只做显示，不参与控制；本模块负责两件事：
 *
 *            1. 下发：把控件名和值拼成 Nextion 指令，末尾补 0xFF 0xFF 0xFF
 *                      例：moudle.t0.txt="P"␃␃␃
 *                          moudle.n0.val=42␃␃␃
 *
 *            2. 接收：解析屏幕上按键回传的指令（预留，目前上层未使用）
 *
 *          ⚠️ 屏幕的 HMI 工程是二进制文件，只能在 USART HMI IDE 里加控件；
 *             本文件里的控件名（t0/t1/n0~n5）必须与 IDE 里实际存在的控件一致，
 *             写错不会报错，只会「发了但屏幕没反应」。
 */

#ifndef SBW_HMI_H
#define SBW_HMI_H

#include <stdint.h>

/* ---------- 1. 缓冲尺寸 ---------- */

#define HMI_RING_BUF_SIZE       ( 256U )    /* 接收环形缓冲：中断写、任务读 */
#define HMI_FRAME_MAX_LEN       ( 64U )     /* 单帧最大长度（含结尾 0xFF×3） */

/* ---------- 2. 对外接口 ---------- */

/**
 * @brief  初始化 LPUART1 并挂起逐字节接收
 * @note   先设中断优先级再开中断——顺序反了会在中断里调用 FreeRTOS API 时断言
 */
void HMI_Init(void);

/**
 * @brief  发送字符串指令：<name>="<value>"
 * @param  name  控件全名，例如 "moudle.t0.txt"
 * @param  value 要显示的字符串
 */
void HMI_SendString(const char *name, const char *value);

/**
 * @brief  发送数值指令：<name>=<value>
 * @param  name  控件全名，例如 "moudle.n0.val"
 * @param  value 整数值
 */
void HMI_SendNumber(const char *name, int32_t value);

/**
 * @brief  发送浮点指令：<name>=<value>
 * @param  name     控件全名
 * @param  value    浮点值
 * @param  decimals 小数位数，超过 6 按 6 处理
 */
void HMI_SendFloat(const char *name, float value, uint8_t decimals);

/**
 * @brief  发送原始文本（不带 0xFF 结尾，仅用于调试输出）
 * @param  text 以 '\0' 结尾的字符串
 */
void HMI_SendText(const char *text);

/**
 * @brief  从接收缓冲取出一条完整指令
 * @param  out    输出缓冲
 * @param  outLen 缓冲长度
 * @retval 1 = 取到一条完整指令，0 = 暂无
 */
uint8_t HMI_GetFrame(char *out, uint16_t outLen);

#endif /* SBW_HMI_H */