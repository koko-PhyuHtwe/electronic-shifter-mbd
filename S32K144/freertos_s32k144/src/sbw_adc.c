/*
 * SBW ADC sampling for S32K144.
 *
 * - ADC0 instance (INST_ADC_CONFIG_1 = 0)
 * - Both physical channels are sampled through control channel 0 (SC1[0]):
 *   software trigger only supports control channel 0, so we re-program it
 *   with each channel's configuration before every conversion.
 * - Sliding average filter (8 samples) + low-end dead band.
 */

#include "sbw_adc.h"

#include "peripherals_adc_config_1.h"
#include "adc_driver.h"

/* =========================================================
 *  Filter parameters
 * ========================================================= */
#define ADC_FILTER_WINDOW   ( 8U )      /* 滑窗点数 (8 x 10ms = 80ms) */
#define ADC_DEAD_LOW        ( 60U )     /* 低端死区: <60 counts (<48mV) 视为 0% */
#define ADC_FULL_SCALE      ( 4095U )   /* 12 bit */

/* =========================================================
 *  Global shared values
 * ========================================================= */
volatile uint16_t g_brake_raw    = 0U;
volatile uint16_t g_throttle_raw = 0U;
volatile uint8_t  g_brake_pct    = 0U;
volatile uint8_t  g_throttle_pct = 0U;

/* =========================================================
 *  Sliding average filter state
 * ========================================================= */
static uint16_t s_brakeWin[ADC_FILTER_WINDOW];
static uint16_t s_throttleWin[ADC_FILTER_WINDOW];
static uint32_t s_brakeSum    = 0U;
static uint32_t s_throttleSum = 0U;
static uint8_t  s_winIdx      = 0U;

/* =========================================================
 *  Init: configure converter + run auto calibration
 * ========================================================= */
void ADC_Init(void)
{
    /* Apply converter configuration (clock/divider/sample time/resolution/trigger/ref). */
    ADC_DRV_ConfigConverter(INST_ADC_CONFIG_1, &adc_config_1_ConvConfig0);

    /* ADC self calibration (sets CAL registers for best linearity). */
    ADC_DRV_AutoCalibration(INST_ADC_CONFIG_1);
}

/* =========================================================
 *  Helper: apply dead band + scale 0~4095 to 0~100 %
 * ========================================================= */
static uint8_t ADC_RawToPercent(uint16_t raw)
{
    if (raw < ADC_DEAD_LOW)
    {
        return 0U;
    }
    return (uint8_t)(((uint32_t)(raw - ADC_DEAD_LOW) * 100U)
                     / (uint32_t)(ADC_FULL_SCALE - ADC_DEAD_LOW));
}

/* =========================================================
 *  Helper: sliding average update
 * ========================================================= */
static uint16_t ADC_FilterAdd(uint16_t win[], uint32_t *sum, uint16_t raw)
{
    *sum -= win[s_winIdx];
    win[s_winIdx] = raw;
    *sum += raw;
    return (uint16_t)(*sum / ADC_FILTER_WINDOW);
}

/* =========================================================
 *  Sample both channels once (blocking, ~几十 us)
 * ========================================================= */
void ADC_SampleAll(void)
{
    uint16_t raw;
    uint16_t avg;

    /* --- Channel: brake pedal, PTA6 = ADC_INPUTCHAN_EXT2 --- */
    ADC_DRV_ConfigChan(INST_ADC_CONFIG_1, 0U, &adc_config_1_ChnConfig0); /* 写 SC1[0] 即触发转换 */
    ADC_DRV_WaitConvDone(INST_ADC_CONFIG_1);
    ADC_DRV_GetChanResult(INST_ADC_CONFIG_1, 0U, &raw);

    avg = ADC_FilterAdd(s_brakeWin, &s_brakeSum, raw);
    g_brake_raw = avg;
    g_brake_pct = ADC_RawToPercent(avg);

    /* --- Channel: throttle, PTA7 = ADC_INPUTCHAN_EXT3 --- */
    ADC_DRV_ConfigChan(INST_ADC_CONFIG_1, 0U, &adc_config_1_ChnConfig1);
    ADC_DRV_WaitConvDone(INST_ADC_CONFIG_1);
    ADC_DRV_GetChanResult(INST_ADC_CONFIG_1, 0U, &raw);

    avg = ADC_FilterAdd(s_throttleWin, &s_throttleSum, raw);
    g_throttle_raw = avg;
    g_throttle_pct = ADC_RawToPercent(avg);

    s_winIdx = (uint8_t)((s_winIdx + 1U) % ADC_FILTER_WINDOW);
}