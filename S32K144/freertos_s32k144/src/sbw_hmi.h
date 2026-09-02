#ifndef SBW_HMI_H
#define SBW_HMI_H

#include <stdint.h>

#define HMI_RING_BUF_SIZE   256U
#define HMI_FRAME_MAX_LEN   64U

void HMI_Init(void);

void HMI_SendString(const char* name, const char* value);
void HMI_SendNumber(const char* name, int32_t value);
void HMI_SendFloat(const char* name, float value, uint8_t decimals);
void HMI_SendText(const char* text);

uint8_t HMI_GetFrame(char* out, uint16_t outLen);

#endif /* SBW_HMI_H */
