#ifndef GREENPAK_HOST_H
#define GREENPAK_HOST_H

#include "stm32g4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define GREENPAK_I2C_ADDRESS_7BIT            0x08u
#define GREENPAK_HOST_REG_OUTPUTS            0x0061u
#define GREENPAK_HOST_OUTPUT_WDT_INDEX        0u
#define GREENPAK_HOST_OUTPUT_RELAY_PWR_INDEX  1u

typedef struct
{
    bool ok;
    uint8_t raw;
    bool wdtOutputLevel;
    bool relayPwrOutputLevel;
    bool wdtAutoToggleEnabled;
} GreenPakHost_OutputStatus_t;

void GreenPakHost_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef GreenPakHost_ReadOutputs(GreenPakHost_OutputStatus_t *status);
HAL_StatusTypeDef GreenPakHost_SetOutput(uint8_t outputIndex, bool high);
HAL_StatusTypeDef GreenPakHost_ToggleOutput(uint8_t outputIndex, bool *newLevel);
HAL_StatusTypeDef GreenPakHost_ToggleRelayPowerEnable(bool *newLevel);
HAL_StatusTypeDef GreenPakHost_SetRelayPowerEnable(bool enable);
HAL_StatusTypeDef GreenPakHost_SetWdtAutoToggle(bool enable);
bool GreenPakHost_GetWdtAutoToggleEnabled(void);
HAL_StatusTypeDef GreenPakHost_ServiceWdtAutoToggle(uint32_t nowMs);
uint32_t GreenPakHost_GetLastError(void);

#endif /* GREENPAK_HOST_H */
