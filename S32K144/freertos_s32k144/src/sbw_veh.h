/**
 * @file    sbw_veh.h
 * @brief   手动挡车辆模型 —— 由油门/刹车/挡位推出车速与转速
 *
 * 台架上没有真车，车速和转速只能算出来。模型采用 6 挡手动变速箱：
 *   · 每个挡位有固定传动比，转速 = 车速 × 传动比 × 主减速比
 *   · 驱动力由发动机扭矩曲线决定，受空气阻力/滚阻/刹车力平衡
 *   · 6 挡 100% 油门时最高车速约 240 km/h
 *
 * 将来接 CANoe 仿真车或真实 CAN 后，整个模块可以删掉，
 * 改成从 CAN 帧里解析 g_vehicle_speed / g_engine_rpm。
 */

#ifndef SBW_VEH_H
#define SBW_VEH_H

#include <stdint.h>

/* ---------- 1. 对外信号 ---------- */

/* 车速，单位 km/h（始终为正值，倒挡也只给大小） */
extern volatile uint16_t g_vehicle_speed;

/* 发动机转速，单位 rpm */
extern volatile uint16_t g_engine_rpm;

/* ---------- 2. 对外接口 ---------- */

/**
 * @brief  复位车辆模型：车速清零、转速置怠速
 * @note   上电调用一次
 */
void VehModel_Init(void);

/**
 * @brief  推进一个积分步长（内部按 100ms 设计），每 100ms 调用一次
 * @param  gearCode 换挡状态机的当前挡位，编码与 GearShift_Stateflow_Y.current_gear 一致：
 *                    0        = P   → 空挡
 *                    1        = R   → 倒挡（传动比与 1 挡相同，方向相反）
 *                    2        = N   → 空挡
 *                    3        = D   → 自动挡（按转速自动升降挡 1~6）
 *                    11 ~ 16  = M1 ~ M6 → 手动挡，用对应传动比
 *                    其他(255)= FAULT → 空挡
 */
void VehModel_Update(uint8_t gearCode);

#endif /* SBW_VEH_H */