/*
 * SBW 占位车辆/发动机模型。
 *
 * 简化关系（够用即可，非真实动力学）：
 *   转速 rpm = 怠速 800 + 油门% × 70        （油门100% => 7800 rpm）
 *   车速 v   += 油门%/3  -  刹车%/2          （油门加速、刹车减速，一阶积分）
 *   车速夹在 0 ~ 240 km/h
 *
 * 调用频率：每 100ms 调用一次 VehModel_Update()（与屏刷新同步）。
 */

#include "sbw_veh.h"
#include "sbw_adc.h"   /* g_brake_pct, g_throttle_pct */

#define RPM_IDLE       ( 800U )   /* 怠速转速 */
#define RPM_GAIN       ( 70U  )   /* 每 1% 油门对应的 rpm */
#define SPEED_MAX      ( 240U )   /* 车速上限 km/h */

volatile uint16_t g_vehicle_speed = 0U;
volatile uint16_t g_engine_rpm    = 0U;

void VehModel_Init(void)
{
    g_vehicle_speed = 0U;
    g_engine_rpm    = RPM_IDLE;
}

void VehModel_Update(void)
{
    int32_t v;
    int32_t rpm;
    int32_t rpm_target;

    /* 转速：目标值随油门线性，但实际转速按一阶惯性逼近，
     * 松油门后不会瞬间掉回怠速，而是缓慢回落（模拟发动机转动惯量）。 */
    rpm_target = (int32_t)RPM_IDLE + (int32_t)g_throttle_pct * (int32_t)RPM_GAIN;
    rpm = (int32_t)g_engine_rpm + (rpm_target - (int32_t)g_engine_rpm) / 10;

    if (rpm < (int32_t)RPM_IDLE)
    {
        rpm = (int32_t)RPM_IDLE;
    }
    g_engine_rpm = (uint16_t)rpm;

    /* 车速：油门加速 - 刹车减速。 */
    v = (int32_t)g_vehicle_speed + (int32_t)(g_throttle_pct / 3U)
                                  - (int32_t)(g_brake_pct   / 2U);

    if (v < 0)
    {
        v = 0;
    }
    else if (v > (int32_t)SPEED_MAX)
    {
        v = (int32_t)SPEED_MAX;
    }

    g_vehicle_speed = (uint16_t)v;
}