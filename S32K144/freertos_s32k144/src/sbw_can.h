/***********************************************************************************************************************
 * @file    sbw_can.h
 * @brief   SBW 项目 FlexCAN2 通信接口
 * @details 基于 S32K144 FlexCAN2，中断接收 + 阻塞发送
 **********************************************************************************************************************/

#ifndef SBW_CAN_H
#define SBW_CAN_H

#include "sdk_project_config.h"
#include <string.h>
#include <stdint.h>

/* CAN 硬件实例和邮箱索引 */
#define CAN_INSTANCE        INST_FLEXCAN_CONFIG_1   /* FlexCAN2 */
#define CAN_TX_MB_IDX       0U                      /* 发送邮箱 M0 */
#define CAN_RX_MB_IDX       1U                      /* 接收邮箱 M1 */
#define CAN_TX_TIMEOUT_MS   100U                    /* 发送超时 ms */

/* SBW 通信 ID */
#define CAN_TX_ID_SBW       0x180U                  /* S32K144 发送给 G474 的换挡状态帧 */
#define CAN_RX_ID_SBW       0x280U                  /* G474 发送给 S32K144 的 HIL 反馈帧 */

/* 接收环形队列深度 */
#define CAN_RX_QUEUE_SIZE   16U

/* 调试计数（可通过串口屏或调试器观察） */
extern volatile uint32_t g_canTxCount;
extern volatile uint32_t g_canTxFailCount;
extern volatile uint32_t g_canRxCount;

/* 统一 CAN 帧结构体 */
typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} can_frame_t;

/***********************************************************************************************************************
 * API 函数声明
 **********************************************************************************************************************/

/* 初始化 FlexCAN2：配置收发邮箱、安装中断回调、启动接收 */
void CAN_Init(void);

/* 反初始化 FlexCAN2 */
void CAN_Deinit(void);

/* 发送一帧 CAN 消息（阻塞式） */
void CAN_SendMessage(uint32_t id, const uint8_t *data, uint8_t len);

/* 查询接收队列是否有数据 */
uint8_t CAN_RxAvailable(void);

/* 从接收队列读取一帧数据 */
void CAN_GetRxFrame(can_frame_t *frame);

#endif /* SBW_CAN_H */
