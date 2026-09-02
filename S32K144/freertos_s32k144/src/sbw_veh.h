#ifndef SBW_VEH_H
#define SBW_VEH_H

#include <stdint.h>

/*
 * SBW 占位车辆/发动机模型（临时模块）。
 * 用油门/刹车 ADC 值算出一个粗略的车速和转速，
 * 供 Stateflow 决策用。将来接 CANoe 独立仿真时，整个本模块删除，
 * 改为从 CAN 帧解析真实的车速/转速即可。
 */

/* 单位: km/h */
extern volatile uint16_t g_vehicle_speed;

/* 单位: rpm */
extern volatile uint16_t g_engine_rpm;

void VehModel_Init(void);
void VehModel_Update(void);

#endif /* SBW_VEH_H */