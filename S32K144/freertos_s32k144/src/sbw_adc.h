#ifndef SBW_ADC_H
#define SBW_ADC_H

#include <stdint.h>

/*
 * SBW ADC module (S32K144):
 *  - PTA6 = ADC0 EXT2 (SE2) = brake pedal   (刹车踏板)
 *  - PTA7 = ADC0 EXT3 (SE3) = throttle      (油门踏板)
 *  - 12 bit, 0~3.3V full scale, software trigger
 */

/* Global shared values (updated every 10 ms by vTask_ADC_Sample). */
extern volatile uint16_t g_brake_raw;     /* 原始采样值 0~4095 */
extern volatile uint16_t g_throttle_raw;  /* 原始采样值 0~4095 */
extern volatile uint8_t  g_brake_pct;     /* 0~100 % */
extern volatile uint8_t  g_throttle_pct;  /* 0~100 % */

void ADC_Init(void);
void ADC_SampleAll(void);

#endif /* SBW_ADC_H */