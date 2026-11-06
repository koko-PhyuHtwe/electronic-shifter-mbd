/**
 * @file    sbw_adc.c
 * @brief   SBW 模拟量采集 —— 滤波、百分比换算、摇杆判向与上电自校准
 *
 * @details ADC0 四通道全部走软件触发，且只有转换控制通道 0（SC1[0]）可用，
 *          所以每次转换前都要 ADC_DRV_ConfigChan() 重新装载通道。
 *
 *          两条处理链路：
 *            刹车 / 油门：8 点滑动平均 → 低端死区 → 0~100 %
 *            摇杆两轴  ：4 点滑动平均 → 相对回中基准的行程百分比 → 三态 + 迟滞
 *
 *          为什么摇杆不能直接用固定阈值：
 *            电位器回中值实测 2936 / 3051（不是中点 2048），并且电位器在机械行程
 *            一半左右就已到达电气尽头。用固定阈值会出现「松手即判成推到底」，
 *            表现为不用解锁也能换挡、M 挡乱跳。因此改为上电自动采集基准，
 *            再按相对基准的行程比例判向。
 *
 * @note    上电后前 100 次采样（1 s）为校准期，期间摇杆一律判中位（读数 0）。
 */

#include "sbw_adc.h"

#include <stdbool.h>

#include "peripherals_adc_config_1.h"
#include "adc_driver.h"

/* ---------- 1. 常量 ---------- */

#define ADC_FILTER_WINDOW   ( 8U )      /* 刹车/油门滑窗点数：8 × 10 ms = 80 ms */
#define ADC_DEAD_LOW        ( 200U )    /* 低端死区：< 200 counts(≈161 mV) 视为 0 %
                                         * 抬高死区是为了吃掉悬空引脚与噪声：
                                         * 踏板未接时引脚会浮到约 1.65 V，
                                         * 不设死区会被当成 50 % 油门，车会自己窜出去 */
#define ADC_FULL_SCALE      ( 4095U )   /* 12 位满量程 */

#define STICK_FILTER_WIN    ( 4U   )    /* 摇杆滑窗点数：4 × 10 ms = 40 ms */
#define STICK_CAL_SAMPLES   ( 100U )    /* 上电校准样本数：100 × 10 ms = 1 s */
#define STICK_TRIGGER_PCT   ( 95   )    /* 行程百分比门限：走到相对基准的 95 % 才判方向 */
#define STICK_HYST          ( 120  )    /* 迟滞量：保持同一方向时把边界内侧收紧 120 counts */

/* 回中基准的合理区间。
 * 电位器正常接好时回中值一定落在量程中部；若上电那 1 s 内电位器没供电或线没接好，
 * 采到的基准会贴着 0 或 4095，用这种基准算门限会导致整段行程都判不出方向。
 * 因此超出区间就丢弃重采，不做「将错就错」。 */
#define STICK_REST_MIN      ( 600U )
#define STICK_REST_MAX      ( 3500U )

/* ---------- 2. 对外信号 ---------- */

volatile uint16_t g_brake_raw    = 0U;
volatile uint16_t g_throttle_raw = 0U;
volatile uint8_t  g_brake_pct    = 0U;
volatile uint8_t  g_throttle_pct = 0U;

volatile uint8_t  g_stick_x      = 0U;
volatile uint8_t  g_stick_y      = 0U;

volatile uint16_t g_stick_x_raw  = 0U;
volatile uint16_t g_stick_y_raw  = 0U;
volatile uint16_t g_stick_rest_x = 0U;      /* 0 = 尚未校准完成 */
volatile uint16_t g_stick_rest_y = 0U;
volatile uint16_t g_stick_cal_cnt = 0U;
volatile uint16_t g_stick_cal_rej = 0U;

/* ---------- 3. 私有变量 ---------- */

/* 刹车 / 油门滑窗 */
static uint16_t s_brakeWin[ADC_FILTER_WINDOW];
static uint16_t s_throttleWin[ADC_FILTER_WINDOW];
static uint32_t s_brakeSum    = 0U;
static uint32_t s_throttleSum = 0U;
static uint8_t  s_winIdx      = 0U;

/* 摇杆两轴滑窗 */
static uint16_t s_stickXWin[STICK_FILTER_WIN];
static uint16_t s_stickYWin[STICK_FILTER_WIN];
static uint32_t s_stickXSum   = 0U;
static uint32_t s_stickYSum   = 0U;
static uint8_t  s_stickWinIdx = 0U;

/* 上电校准状态：X / Y 两轴各自独立，一个轴没接好不拖累另一个轴 */
static uint32_t s_calXSum = 0U;
static uint32_t s_calYSum = 0U;
static uint16_t s_calCntX = 0U;
static uint16_t s_calCntY = 0U;
static uint16_t s_restX   = 2048U;    /* 校准完成前的占位值，不会被使用 */
static uint16_t s_restY   = 2048U;

/* ---------- 4. 私有函数 ---------- */

/**
 * @brief  原始值换算成 0~100 %
 * @param  raw 12 位原始值
 * @retval 0~100，低于死区一律返回 0
 */
static uint8_t ADC_RawToPercent(uint16_t raw)
{
    if (raw < ADC_DEAD_LOW)
    {
        return 0U;
    }

    return (uint8_t)(((uint32_t)(raw - ADC_DEAD_LOW) * 100U)
                     / (uint32_t)(ADC_FULL_SCALE - ADC_DEAD_LOW));
}

/**
 * @brief  刹车 / 油门滑动平均，返回当前窗均值
 * @param  win 窗口数组
 * @param  sum 窗口累加和（调用方维护，避免每次重新求和）
 * @param  raw 本次采样值
 * @retval 窗口算术平均
 */
static uint16_t ADC_FilterAdd(uint16_t win[], uint32_t *sum, uint16_t raw)
{
    *sum -= win[s_winIdx];
    win[s_winIdx] = raw;
    *sum += raw;

    return (uint16_t)(*sum / ADC_FILTER_WINDOW);
}

/**
 * @brief  摇杆通道滑动平均，返回当前窗均值
 * @param  win 窗口数组
 * @param  sum 窗口累加和
 * @param  raw 本次采样值
 * @retval 窗口算术平均
 */
static uint16_t StickFilterAdd(uint16_t win[], uint32_t *sum, uint16_t raw)
{
    *sum -= win[s_stickWinIdx];
    win[s_stickWinIdx] = raw;
    *sum += raw;

    return (uint16_t)(*sum / STICK_FILTER_WIN);
}

/**
 * @brief  相对回中基准判定摇杆方向
 * @param  adc  滤波后的采样值
 * @param  rest 该轴的单一回中基准（校准所得）
 * @param  prev 该轴上一次的判定结果，用于迟滞
 * @retval 0 = 中位，1 = 高电压侧（后拉 / 左推），2 = 低电压侧（前推 / 右推）
 *
 * @details 门限按「相对基准的行程」计算：
 *            lo = rest - rest × 95 %
 *            hi = rest + (4095 - rest) × 95 %
 *          这样即使回中值偏到 2936，两侧门限也会随之平移，不会一边够不到、
 *          一边一碰就触发。prev 不为 0 时把边界往里收 STICK_HYST，
 *          形成迟滞区，避免停在临界点时方向反复翻转。
 */
static uint8_t ADC_ToStickPos(uint16_t adc, uint16_t rest, uint8_t prev)
{
    int32_t lo = (int32_t)rest - ((int32_t)rest * STICK_TRIGGER_PCT / 100);
    int32_t hi = (int32_t)rest
               + (((int32_t)ADC_FULL_SCALE - (int32_t)rest) * STICK_TRIGGER_PCT / 100);

    if (lo < 0)
    {
        lo = 0;
    }
    if (hi > (int32_t)ADC_FULL_SCALE)
    {
        hi = (int32_t)ADC_FULL_SCALE;
    }

    /* 迟滞：已经在某个方向上时，把该方向的门限往里收，形成回差 */
    if (prev == 2U)
    {
        lo += STICK_HYST;
    }
    if (prev == 1U)
    {
        hi -= STICK_HYST;
    }

    if ((int32_t)adc < lo)
    {
        return 2U;
    }
    if ((int32_t)adc > hi)
    {
        return 1U;
    }

    return 0U;
}

/* ---------- 5. 对外接口 ---------- */

/**
 * @brief  上电初始化：装载转换器配置并执行自校准
 * @note   必须先于 ADC_SampleAll() 调用，否则读不到有效数据
 */
void ADC_Init(void)
{
    /* 时钟/分频/采样时间/分辨率/触发方式/参考源都在 ConvConfig0 里 */
    ADC_DRV_ConfigConverter(INST_ADC_CONFIG_1, &adc_config_1_ConvConfig0);

    /* ADC 内部自校准，消除增益与偏移误差 */
    ADC_DRV_AutoCalibration(INST_ADC_CONFIG_1);
}

/**
 * @brief  采样四路模拟量并更新全部对外信号
 * @note   每 10 ms 调用一次。上电后前 1 s 为摇杆校准期，期间两轴强制判中位。
 */
void ADC_SampleAll(void)
{
    uint16_t raw;
    uint16_t avg;

    /* ---- 通道 0：刹车踏板 PTA6 = EXT2 ---- */
    ADC_DRV_ConfigChan(INST_ADC_CONFIG_1, 0U, &adc_brake);
    ADC_DRV_WaitConvDone(INST_ADC_CONFIG_1);
    ADC_DRV_GetChanResult(INST_ADC_CONFIG_1, 0U, &raw);

    avg = ADC_FilterAdd(s_brakeWin, &s_brakeSum, raw);
    g_brake_raw = avg;
    g_brake_pct = ADC_RawToPercent(avg);

    /* ---- 通道 1：油门踏板 PTA7 = EXT3 ---- */
    ADC_DRV_ConfigChan(INST_ADC_CONFIG_1, 0U, &adc_speed);
    ADC_DRV_WaitConvDone(INST_ADC_CONFIG_1);
    ADC_DRV_GetChanResult(INST_ADC_CONFIG_1, 0U, &raw);

    avg = ADC_FilterAdd(s_throttleWin, &s_throttleSum, raw);
    g_throttle_raw = avg;
    g_throttle_pct = ADC_RawToPercent(avg);

    /* 刹车与油门共用同一个窗索引 */
    s_winIdx = (uint8_t)((s_winIdx + 1U) % ADC_FILTER_WINDOW);

    /* ---- 通道 2：摇杆 X 轴 PTC2 = EXT10 ---- */
    ADC_DRV_ConfigChan(INST_ADC_CONFIG_1, 0U, &adc_x);
    ADC_DRV_WaitConvDone(INST_ADC_CONFIG_1);
    ADC_DRV_GetChanResult(INST_ADC_CONFIG_1, 0U, &raw);

    avg = StickFilterAdd(s_stickXWin, &s_stickXSum, raw);
    g_stick_x_raw = avg;

    /* ---- 通道 3：摇杆 Y 轴 PTC3 = EXT11 ---- */
    ADC_DRV_ConfigChan(INST_ADC_CONFIG_1, 0U, &adc_y);
    ADC_DRV_WaitConvDone(INST_ADC_CONFIG_1);
    ADC_DRV_GetChanResult(INST_ADC_CONFIG_1, 0U, &raw);

    avg = StickFilterAdd(s_stickYWin, &s_stickYSum, raw);
    g_stick_y_raw = avg;

    s_stickWinIdx = (uint8_t)((s_stickWinIdx + 1U) % STICK_FILTER_WIN);

    /* ---- 上电校准：两轴各自累加 100 个样本求平均 ---- */
    if (s_calCntX < STICK_CAL_SAMPLES)
    {
        s_calXSum += g_stick_x_raw;
        s_calCntX++;

        if (s_calCntX >= STICK_CAL_SAMPLES)
        {
            uint16_t rest = (uint16_t)(s_calXSum / STICK_CAL_SAMPLES);

            if ((rest >= STICK_REST_MIN) && (rest <= STICK_REST_MAX))
            {
                s_restX = rest;
                g_stick_rest_x = s_restX;
            }
            else
            {
                /* 基准不合理（电位器没供电 / 没接地）：清空重采，
                 * 期间 g_stick_rest_x 保持 0，屏幕据此显示 STICK CAL */
                s_calXSum = 0U;
                s_calCntX = 0U;
                g_stick_cal_rej++;
            }
        }

        g_stick_x = 0U;             /* 校准期间一律判中位 */
    }
    else
    {
        g_stick_x = ADC_ToStickPos(g_stick_x_raw, s_restX, g_stick_x);
    }

    if (s_calCntY < STICK_CAL_SAMPLES)
    {
        s_calYSum += g_stick_y_raw;
        s_calCntY++;

        if (s_calCntY >= STICK_CAL_SAMPLES)
        {
            uint16_t rest = (uint16_t)(s_calYSum / STICK_CAL_SAMPLES);

            if ((rest >= STICK_REST_MIN) && (rest <= STICK_REST_MAX))
            {
                s_restY = rest;
                g_stick_rest_y = s_restY;
            }
            else
            {
                s_calYSum = 0U;
                s_calCntY = 0U;
                g_stick_cal_rej++;
            }
        }

        g_stick_y = 0U;
    }
    else
    {
        g_stick_y = ADC_ToStickPos(g_stick_y_raw, s_restY, g_stick_y);
    }

    /* 供屏幕排查：0~100，到达 100 表示 X 轴校准完成 */
    g_stick_cal_cnt = s_calCntX;
}