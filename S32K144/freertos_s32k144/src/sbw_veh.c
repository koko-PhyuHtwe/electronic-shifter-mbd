/**
 * @file    sbw_veh.c
 * @brief   6 挡手动变速箱车辆模型
 *
 * 一、传动链
 *     车速 v(m/s) → 车轮转速 = v / (2πR)
 *     发动机转速 rpm = 车轮转速 × 60 × 挡位传动比 × 主减速比
 *     所以同一车速下，挡位越低转速越高 —— 这就是"手动挡逻辑"。
 *
 * 二、动力
 *     驱动力 F = 扭矩 × 传动比 × 主减速比 × 传动效率 / 车轮半径
 *     阻力   R = 0.34·v² + 200 + 刹车力
 *     加速度 a = (F − R) / 1400kg
 *
 * 三、挡位来源（gearCode 与 GearShift_Stateflow_Y.current_gear 一致）
 *     M1~M6 (11~16) 手动挡，直接用对应传动比
 *     D     (3)     自动挡，按转速自动升/降挡
 *     R     (1)     倒挡，传动比同 1 挡、方向相反
 *     P/N/F (0/2/255) 空挡，发动机自由转速
 *
 * 四、标定目标：6 挡 100% 油门时最高车速约 240 km/h。
 *     240 km/h 时 6 挡转速约 5480 rpm，驱动力约 1700 N，
 *     由 DRAG_COEF=0.34、ROLL_RESIST_N=200 平衡。
 */

#include "sbw_veh.h"
#include "sbw_adc.h"        /* g_throttle_pct, g_brake_pct */

#include <stdbool.h>

/* ---------- 1. 整车参数 ---------- */
#define VEH_MASS_KG         (  1400.0f )   /* 整车质量 */
#define WHEEL_RADIUS_M      (     0.31f )  /* 车轮滚动半径 */
#define DRIVELINE_EFF       (     0.92f )  /* 传动效率 */
#define DRAG_COEF           (     0.34f )  /* 空气阻力系数 N/(m/s)² */
#define ROLL_RESIST_N       (   200.0f )   /* 滚动阻力 */
#define BRAKE_FORCE_MAX_N   (  9000.0f )   /* 刹车 100% 时的制动力 */
#define SPEED_MAX_KMH       (   240.0f )   /* 车速上限 */
#define VEH_DT_S            (     0.1f )   /* 积分步长，与 100ms 调用周期一致 */

/* ---------- 2. 演示手感标定（不影响极速，只影响加速快慢） ---------- */
/* 等效质量放大 N 倍：加速过程慢 N 倍，稳态最高车速不变。
 * 1400kg × 3 ≈ 4200kg，0~100km/h 约 9 秒，接近一台普通家用车。 */
#define DEMO_INERTIA_SCALE  (     3.0f )
/* 油门斜坡：每秒最多变化多少（1.0 = 满油门 1 秒）。防止电位器一拧就蹿。 */
#define THROTTLE_RAMP_PER_S (     0.9f )

/* ---------- 3. 发动机参数 ---------- */
#define ENGINE_TORQUE_MAX   (   250.0f )   /* 峰值扭矩 Nm */
#define ENGINE_BRAKE_TORQUE (    28.0f )   /* 松油门时的发动机制动扭矩 Nm */
#define RPM_IDLE            (   800.0f )   /* 怠速 */
#define RPM_REDLINE         (  6800.0f )   /* 红线 */
#define RPM_FUEL_CUT        (  6700.0f )   /* 断油转速 */
#define RPM_FUEL_RESUME     (  6300.0f )   /* 恢复供油转速（滞回） */
#define RPM_LAUNCH_SPAN     (  2500.0f )   /* 半联动时油门带来的转速增量 */
#define RPM_INERTIA_K       (     0.35f )  /* 转速一阶惯性系数 */

/* ---------- 4. 变速箱 ---------- */
#define FINAL_DRIVE         (     3.90f )  /* 主减速比 */
#define GEAR_NEUTRAL        (      0U )
#define GEAR_REVERSE        (      7U )
#define GEAR_MANUAL_MAX     (      6U )

/* 传动比表：0=空挡，1~6=前进挡，7=倒挡 */
static const float kGearRatio[8] =
{
    0.000f,     /* 0 空挡 */
    3.545f,     /* 1 挡 */
    2.105f,     /* 2 挡 */
    1.357f,     /* 3 挡 */
    1.000f,     /* 4 挡 */
    0.822f,     /* 5 挡 */
    0.685f,     /* 6 挡 */
    3.545f      /* 7 倒挡 */
};

#define TWO_PI_R            ( 6.28318531f * WHEEL_RADIUS_M )

/* ---------- 5. 内部状态 ---------- */
static float   s_speedMps   = 0.0f;             /* 有符号车速，R 挡为负 */
static float   s_engineRpm  = RPM_IDLE;
static float   s_throttle   = 0.0f;             /* 斜坡后的油门 0~1 */
static uint8_t s_autoGear   = 1U;               /* D 挡自动挡位 */
static bool    s_fuelCut    = false;

/* ---------- 6. 对外信号 ---------- */
volatile uint16_t g_vehicle_speed = 0U;
volatile uint16_t g_engine_rpm    = 0U;

/* ---------- 7. 内部工具函数 ---------- */

/**
 * @brief  绝对值
 * @note   自己实现，避免为了一个 fabs 去链接 libm
 */
static float VehModel_Abs(float x)
{
    return (x < 0.0f) ? (-x) : x;
}

/**
 * @brief  车速 → 发动机转速
 * @param  vMps  车速 m/s
 * @param  ratio 当前挡位传动比
 * @retval 发动机转速 rpm
 */
static float VehModel_RpmFromSpeed(float vMps, float ratio)
{
    return (vMps / TWO_PI_R) * 60.0f * ratio * FINAL_DRIVE;
}

/**
 * @brief  发动机转速 → 车速
 * @param  rpm   发动机转速
 * @param  ratio 挡位传动比
 * @retval 车速 m/s
 * @note   用来判断「该挡怠速车速」，低于它即进入半联动
 */
static float VehModel_SpeedFromRpm(float rpm, float ratio)
{
    return (rpm / (60.0f * ratio * FINAL_DRIVE)) * TWO_PI_R;
}

/**
 * @brief  归一化扭矩系数曲线（8 点线性插值）
 * @param  rpm 发动机转速
 * @retval 0~1 的扭矩系数：低转不足、4000rpm 最饱满、高转衰减
 */
static float VehModel_TorqueFactor(float rpm)
{
    static const float kRpmPt[8] = { 800.0f, 1200.0f, 2000.0f, 3000.0f,
                                     4000.0f, 5000.0f, 6000.0f, 6800.0f };
    static const float kFacPt[8] = {  0.45f,   0.62f,   0.82f,   0.98f,
                                      1.00f,   0.93f,   0.82f,   0.72f };
    uint8_t i;

    if (rpm <= kRpmPt[0])
    {
        return kFacPt[0];
    }
    if (rpm >= kRpmPt[7])
    {
        return kFacPt[7];
    }

    for (i = 0U; i < 7U; i++)
    {
        if (rpm <= kRpmPt[i + 1U])
        {
            float t = (rpm - kRpmPt[i]) / (kRpmPt[i + 1U] - kRpmPt[i]);
            return kFacPt[i] + (t * (kFacPt[i + 1U] - kFacPt[i]));
        }
    }

    return kFacPt[7];
}

/**
 * @brief  D 挡自动换挡：高于 2400rpm 升挡，低于 1200rpm 降挡
 * @param  vMps 当前车速 m/s（有符号）
 * @note   只在 gearCode == 3（D）时调用；M 挡由状态机按门槛自行升降
 */
static void VehModel_AutoShift(float vMps)
{
    float rpm = VehModel_RpmFromSpeed(VehModel_Abs(vMps), kGearRatio[s_autoGear]);

    if ((rpm > 2400.0f) && (s_autoGear < GEAR_MANUAL_MAX))
    {
        s_autoGear++;
    }
    else if ((rpm < 1200.0f) && (s_autoGear > 1U))
    {
        s_autoGear--;
    }
}

/* ---------- 8. 对外函数 ---------- */

/**
 * @brief  复位车辆模型：车速归零、发动机回怠速
 * @note   上电调用一次，保证第一次 VehModel_Update 不会从随机值开始积分
 */
void VehModel_Init(void)
{
    s_speedMps  = 0.0f;
    s_engineRpm = RPM_IDLE;
    s_throttle  = 0.0f;
    s_autoGear  = 1U;
    s_fuelCut   = false;

    g_vehicle_speed = 0U;
    g_engine_rpm    = (uint16_t)RPM_IDLE;
}

/**
 * @brief  推进一个计算步（100ms）
 * @param  gearCode 当前挡位码，来自 GearShift_Stateflow_Y.current_gear
 *
 * @details 七个步骤：
 *            0. 油门斜坡 —— 防止电位器一拧就蹿，演示时更可控
 *            1. 解析挡位 —— D 走自动挡位，M1~M6 用固定传动比，R 取 1 挡比反向
 *            2. 目标转速 —— 空挡只看油门；有挡且低于怠速车速时按半联动处理
 *            3. 断油保护 —— 到 6700 断油、掉到 6300 恢复（滞回，避免抖动）
 *            4. 驱动力   —— 扭矩曲线 × 油门 × 传动比 ÷ 车轮半径
 *            5. 阻力积分 —— 风阻 + 滚阻 + 刹车力，除以质量得加速度
 *            6. 边界处理 —— 防止反向惯性、限速 240km/h
 *            7. 输出     —— 换算成 km/h 与 rpm 写回全局变量
 */
void VehModel_Update(uint8_t gearCode)
{
    float       throttle = (float)g_throttle_pct / 100.0f;   /* 0~1，斜坡前 */
    const float brake    = (float)g_brake_pct    / 100.0f;   /* 0~1 */

    float   absV;
    float   ratio;
    float   rpmTarget;
    float   torque;
    float   driveN;
    float   resistN;
    float   brakeN;
    float   dir;
    float   net;
    uint8_t gear;

    /* ---------- 0. 油门斜坡（演示手感） ---------- */
    {
        float dThr   = throttle - s_throttle;
        float dMax   = THROTTLE_RAMP_PER_S * VEH_DT_S;

        if (dThr > dMax)
        {
            dThr = dMax;
        }
        else if (dThr < -dMax)
        {
            dThr = -dMax;
        }
        s_throttle += dThr;
        throttle = s_throttle;
    }

    /* ---------- 1. 解析挡位 ---------- */
    if (gearCode == 3U)                                 /* D：自动挡 */
    {
        VehModel_AutoShift(s_speedMps);
        gear = s_autoGear;
    }
    else if ((gearCode >= 11U) && (gearCode <= 16U))    /* M1~M6 */
    {
        gear = (uint8_t)(gearCode - 10U);
    }
    else if (gearCode == 1U)                            /* R */
    {
        gear = GEAR_REVERSE;
    }
    else                                                /* P / N / FAULT */
    {
        gear = GEAR_NEUTRAL;
    }

    absV  = VehModel_Abs(s_speedMps);
    ratio = kGearRatio[gear];

    /* ---------- 2. 目标转速 ---------- */
    if (gear == GEAR_NEUTRAL)
    {
        /* 空挡：发动机与车轮脱开，转速只跟油门走 */
        rpmTarget = RPM_IDLE + (throttle * (RPM_FUEL_CUT - RPM_IDLE));
    }
    else
    {
        float vIdle = VehModel_SpeedFromRpm(RPM_IDLE, ratio);

        if (absV < vIdle)
        {
            /* 半联动：车速低于该挡怠速对应车速，离合打滑，转速由油门决定 */
            rpmTarget = RPM_IDLE + (throttle * RPM_LAUNCH_SPAN * (1.0f - (absV / vIdle)));
        }
        else
        {
            rpmTarget = VehModel_RpmFromSpeed(absV, ratio);
        }

        if (rpmTarget > RPM_REDLINE)
        {
            rpmTarget = RPM_REDLINE;
        }
        if (rpmTarget < RPM_IDLE)
        {
            rpmTarget = RPM_IDLE;
        }
    }

    s_engineRpm += (rpmTarget - s_engineRpm) * RPM_INERTIA_K;
    if (s_engineRpm < RPM_IDLE)
    {
        s_engineRpm = RPM_IDLE;
    }

    /* ---------- 3. 断油保护（带滞回） ---------- */
    if (s_engineRpm > RPM_FUEL_CUT)
    {
        s_fuelCut = true;
    }
    else if (s_engineRpm < RPM_FUEL_RESUME)
    {
        s_fuelCut = false;
    }

    /* ---------- 4. 驱动力 ---------- */
    driveN = 0.0f;
    if ((gear != GEAR_NEUTRAL) && (!s_fuelCut))
    {
        float tf = VehModel_TorqueFactor(s_engineRpm);

        if (throttle > 0.02f)
        {
            torque = ENGINE_TORQUE_MAX * tf * throttle;
        }
        else if (absV > 0.3f)
        {
            torque = -ENGINE_BRAKE_TORQUE * tf;      /* 发动机制动 */
        }
        else
        {
            torque = 0.0f;                           /* 停着就别往后推 */
        }

        driveN = torque * ratio * FINAL_DRIVE * DRIVELINE_EFF / WHEEL_RADIUS_M;

        if (gear == GEAR_REVERSE)
        {
            driveN = -driveN;                        /* 倒挡方向相反 */
        }
    }

    /* ---------- 5. 阻力与积分 ---------- */
    dir = (s_speedMps > 0.05f) ? 1.0f : ((s_speedMps < -0.05f) ? -1.0f : 0.0f);

    resistN = ((DRAG_COEF * s_speedMps * s_speedMps) + ROLL_RESIST_N) * dir;
    brakeN  = BRAKE_FORCE_MAX_N * brake * dir;

    net = driveN - resistN - brakeN;

    /* 静止且踩着刹车：刹车力足以压住驱动力，起步不了 */
    if ((dir == 0.0f) && (brake > 0.05f) &&
        (VehModel_Abs(driveN) <= (BRAKE_FORCE_MAX_N * brake)))
    {
        net = 0.0f;
    }

    s_speedMps += (net / (VEH_MASS_KG * DEMO_INERTIA_SCALE)) * VEH_DT_S;

    /* ---------- 6. 边界处理 ---------- */
    if ((dir > 0.0f) && (s_speedMps < 0.0f) && (driveN <= 0.0f))
    {
        s_speedMps = 0.0f;
    }
    if ((dir < 0.0f) && (s_speedMps > 0.0f) && (driveN >= 0.0f))
    {
        s_speedMps = 0.0f;
    }
    if (s_speedMps > (SPEED_MAX_KMH / 3.6f))
    {
        s_speedMps = SPEED_MAX_KMH / 3.6f;
    }
    if (s_speedMps < (-SPEED_MAX_KMH / 3.6f))
    {
        s_speedMps = -SPEED_MAX_KMH / 3.6f;
    }

    /* ---------- 7. 输出 ---------- */
    g_vehicle_speed = (uint16_t)((VehModel_Abs(s_speedMps) * 3.6f) + 0.5f);
    g_engine_rpm    = (uint16_t)(s_engineRpm + 0.5f);
}