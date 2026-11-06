/**
 * @file    sbw_can.h
 * @brief   FlexCAN2 通信接口 —— 换挡指令下发与执行反馈接收
 *
 * @details 物理层：Classic CAN，500 kbps（位时序由 peripherals_flexcan_config_1.c 配置）。
 *          收发方式：发送走邮箱 0（阻塞式），接收走邮箱 1（中断 + 16 槽环形队列）。
 *
 *          两条报文：
 *            0x180  S32K144 → G474   换挡指令 / 车速 / 油门 / 刹车
 *            0x280  G474 → S32K144   实际档位 / 舵机角度 / 状态位 / 生命计数
 */

#ifndef SBW_CAN_H
#define SBW_CAN_H

#include <stdint.h>
#include <string.h>

#include "sdk_project_config.h"

/* ---------- 1. 硬件实例与邮箱 ---------- */

#define CAN_INSTANCE            INST_FLEXCAN_CONFIG_1   /* FlexCAN2 */
#define CAN_TX_MB_IDX           ( 0U )                  /* 发送邮箱 M0 */
#define CAN_RX_MB_IDX           ( 1U )                  /* 接收邮箱 M1 */

/* ---------- 2. 报文 ID ---------- */

#define CAN_TX_ID_SBW           ( 0x180U )  /* 换挡指令帧 */
#define CAN_RX_ID_SBW           ( 0x280U )  /* 执行反馈帧 */

/* ---------- 3. 0x180 负载布局（S32K144 → G474） ---------- */

#define CAN_TX_IDX_GEAR         ( 0U )      /* B0 目标档位 */
#define CAN_TX_IDX_DISPLAY      ( 1U )      /* B1 显示用档位（与 B0 同值） */
                    /* B2 保留 */
#define CAN_TX_IDX_LIFE         ( 3U )      /* B3 生命计数，0~255 循环 */
#define CAN_TX_IDX_SPEED        ( 4U )      /* B4 车速 km/h（0~240） */
#define CAN_TX_IDX_MOTOR        ( 5U )      /* B5 电机油门 %（0~100） */
#define CAN_TX_IDX_BRAKE        ( 6U )      /* B6 刹车力度 %（0~100），执行侧据此断动力 */
                    /* B7 保留 */

/* ---------- 4. 0x280 负载布局（G474 → S32K144） ---------- */

#define CAN_RX_IDX_GEAR         ( 0U )      /* B0 实际档位（舵机真正到位后） */
#define CAN_RX_IDX_ANGLE        ( 1U )      /* B1 舵机角度 0~180 */
#define CAN_RX_IDX_STATUS       ( 2U )      /* B2 执行侧状态位 */
#define CAN_RX_IDX_LIFE         ( 3U )      /* B3 生命计数 */

/* ---------- 5. 0x280 状态位（必须与 G474 侧 STATUS_* 保持一致） ---------- */

#define CAN_STATUS_COMM_TIMEOUT ( 0x01U )   /* bit0 执行侧收不到指令 */
#define CAN_STATUS_SHIFT_REFUSED ( 0x02U )  /* bit1 执行侧拒绝执行 P/R（对端重启锁存） */
#define CAN_STATUS_INVALID_CMD  ( 0x04U )   /* bit2 执行侧收到非法档位码 */
#define CAN_STATUS_MOTOR_CUT    ( 0x08U )   /* bit3 执行侧已软切动力（失联 / FAULT） */
#define CAN_STATUS_GEAR_INHIBIT ( 0x10U )   /* bit4 当前挡位不给动力（P/N/R） */
#define CAN_STATUS_BRAKE_CUT    ( 0x20U )   /* bit5 刹车优先，动力被切断 */

/* ---------- 6. 缓存与调试 ---------- */

#define CAN_RX_QUEUE_SIZE       ( 16U )     /* 接收环形队列深度 */

extern volatile uint32_t g_canTxCount;      /* 发送成功帧数 */
extern volatile uint32_t g_canTxFailCount;  /* 发送失败帧数 */
extern volatile uint32_t g_canRxCount;      /* 接收成功帧数 */

typedef struct
{
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} can_frame_t;

/* ---------- 7. 对外接口 ---------- */

/**
 * @brief  初始化 FlexCAN2：配置收发邮箱、安装中断回调、挂起接收
 * @note   失败时点亮绿灯作为提示，不会死循环
 */
void CAN_Init(void);

/**
 * @brief  反初始化 FlexCAN2
 */
void CAN_Deinit(void);

/**
 * @brief  发送一帧（阻塞式，内部有限等待）
 * @param  id   标准帧 ID
 * @param  data 负载指针
 * @param  len  负载长度 1~8
 */
void CAN_SendMessage(uint32_t id, const uint8_t *data, uint8_t len);

/**
 * @brief  查询接收队列是否有数据
 * @retval 队列中未读帧数
 */
uint8_t CAN_RxAvailable(void);

/**
 * @brief  从接收队列取一帧
 * @param  frame 输出缓冲
 */
void CAN_GetRxFrame(can_frame_t *frame);

/**
 * @brief  读取控制器错误状态，用于区分「线路问题」和「对端不说话」
 * @retval 0 = 正常（error-active）
 *         1 = 错误被动 / 警告（错误计数在涨：终端电阻、线序、共地、波特率偏差）
 *         2 = 总线关闭（收发器没电、CANH/CANL 断或短、总线上没有第二个节点应答）
 *         3 = 控制器未初始化
 */
uint8_t CAN_GetBusState(void);

/**
 * @brief  读取发送 / 接收错误计数器
 * @param  tec 发送错误计数（0~255）
 * @param  rec 接收错误计数（0~255）
 * @note   未初始化时两者返回 0xFF
 */
void CAN_GetErrCounters(uint8_t *tec, uint8_t *rec);

/**
 * @brief  读取 ESR1 错误类型锁存位
 * @retval bit0 STFERR / bit1 FRMERR / bit2 CRCERR / bit3 ACKERR / bit4 BIT0ERR / bit5 BIT1ERR
 */
uint16_t CAN_GetErrFlags(void);

#endif /* SBW_CAN_H */