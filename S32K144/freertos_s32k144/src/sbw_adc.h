/**
 * @file    sbw_adc.h
 * @brief   SBW 模拟量采集（ADC0 四通道）—— 刹车 / 油门 / 摇杆两轴
 *
 * @details 全部为软件触发、单次转换，由 ADC 任务每 10 ms 调用一次 ADC_SampleAll()。
 *          四路共用转换控制通道 0（SC1[0]），因此每次转换前都要重新装载通道配置。
 *
 *          通道分配（12 位，0~3.3 V）：
 *            PTA6（脚 58） ADC0_SE2  → 刹车踏板  brake     0~100 %
 *            PTA7（脚 57） ADC0_SE3  → 油门踏板  throttle  0~100 %
 *            PTC2（脚 30） ADC0_SE10 → 摇杆 X 轴 stick_x   0=中位 1=后拉 2=前推
 *            PTC3（脚 29） ADC0_SE11 → 摇杆 Y 轴 stick_y   0=中位 1=左推 2=右推
 *
 * @note    摇杆虽然是自动回中结构，但实测回中值并不在量程中点
 *          （约 2936 / 3051，而非 2048），因此不能使用固定阈值。
 *          本模块在上电后 1 s 内自动采集回中基准，再按「相对基准的行程百分比」
 *          判向，辅以 4 点滑动滤波和迟滞，具体实现见 sbw_adc.c。
 */

#ifndef SBW_ADC_H
#define SBW_ADC_H

#include <stdint.h>

/* ---------- 1. 对外信号 ---------- */

/* 刹车 / 油门：滤波后的原始值 + 换算后的 0~100 % */
extern volatile uint16_t g_brake_raw;
extern volatile uint16_t g_throttle_raw;
extern volatile uint8_t  g_brake_pct;
extern volatile uint8_t  g_throttle_pct;

/* 摇杆两轴判向结果：0 = 中位，1 = 后拉 / 左推，2 = 前推 / 右推 */
extern volatile uint8_t  g_stick_x;
extern volatile uint8_t  g_stick_y;

/* 摇杆调试量：滤波后的原始值、上电校准得到的回中基准 */
extern volatile uint16_t g_stick_x_raw;
extern volatile uint16_t g_stick_y_raw;
extern volatile uint16_t g_stick_rest_x;    /* 0 表示该轴尚未完成校准 */
extern volatile uint16_t g_stick_rest_y;    /* 0 表示该轴尚未完成校准 */

/* 校准过程量，供屏幕排查：本轮已采样本 0~100、基准被判不合理而重采的次数 */
extern volatile uint16_t g_stick_cal_cnt;
extern volatile uint16_t g_stick_cal_rej;

/* ---------- 2. 对外接口 ---------- */

/**
 * @brief  上电调用一次：装载转换器配置并执行 ADC 自校准
 */
void ADC_Init(void);

/**
 * @brief  每 10 ms 调用一次：依次采样四路，刷新上面所有信号
 * @note   阻塞式采样，单次耗时几十微秒；内部完成滤波、判向与上电校准
 */
void ADC_SampleAll(void);

#endif /* SBW_ADC_H */