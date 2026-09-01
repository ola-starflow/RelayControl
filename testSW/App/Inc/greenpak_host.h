#ifndef GREENPAK_HOST_H
#define GREENPAK_HOST_H

#include "stm32g4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define GREENPAK_I2C_ADDRESS_08_7BIT         0x08u
#define GREENPAK_I2C_ADDRESS_18_7BIT         0x18u
#define GREENPAK_HOST_REG_OUTPUTS            0x0061u
#define GREENPAK_HOST_REG_SIGNAL_READBACK    0x0062u
#define GREENPAK_HOST_OUTPUT_WDT_INDEX       0u
#define GREENPAK_HOST_OUTPUT_RELAY_PWR_INDEX 1u
#define GREENPAK_HOST_OUTPUT_ADC_INDEX       2u

typedef enum
{
    GREENPAK_DEVICE_08 = 0,
    GREENPAK_DEVICE_18,
    GREENPAK_DEVICE_COUNT
} GreenPakHost_Device_t;

typedef struct
{
    bool ok;
    uint8_t raw;
    bool wdtOutputLevel;
    bool relayPwrOutputLevel;
    bool adcOutputLevel;
    bool wdtAutoToggleEnabled;
} GreenPakHost_OutputStatus_t;

typedef struct
{
    bool autoToggleEnabled;
    bool outputLevel;
    uint32_t targetToggleIntervalMs;
    uint32_t lastToggleMs;
    uint32_t lastToggleIntervalMs;
    uint32_t maxToggleIntervalMs;
    uint32_t serviceCallCount;
    uint32_t lastServiceIntervalMs;
    uint32_t maxServiceIntervalMs;
    uint32_t toggleWriteCount;
    uint32_t toggleWriteFailCount;
    uint32_t lastHalError;
} GreenPakHost_WdtDiagnostics_t;

typedef struct
{
    HAL_StatusTypeDef status;
    uint16_t relayPowerContinuousRaw;  /* Data Buffer0 result / moving average */
    uint16_t relayPowerBoostRaw;       /* Data Buffer1 result / moving average */
    uint16_t temperatureRaw;           /* Data Buffer2 result / moving average */
} GreenPakHost_AnalogValues_t;

void GreenPakHost_Init(I2C_HandleTypeDef *hi2c);

GreenPakHost_Device_t GreenPakHost_GetSelectedDevice(void);
void GreenPakHost_SelectDevice(GreenPakHost_Device_t device);
void GreenPakHost_SelectNextDevice(void);
uint8_t GreenPakHost_GetAddress7BitForDevice(GreenPakHost_Device_t device);
uint16_t GreenPakHost_GetAddressHalForDevice(GreenPakHost_Device_t device);
uint8_t GreenPakHost_GetSelectedAddress7Bit(void);
uint16_t GreenPakHost_GetSelectedAddressHal(void);

HAL_StatusTypeDef GreenPakHost_ReadOutputs(GreenPakHost_OutputStatus_t *status);
HAL_StatusTypeDef GreenPakHost_SetOutput(uint8_t outputIndex, bool high);
HAL_StatusTypeDef GreenPakHost_ToggleOutput(uint8_t outputIndex, bool *newLevel);
HAL_StatusTypeDef GreenPakHost_ToggleRelayPowerEnable(bool *newLevel);
HAL_StatusTypeDef GreenPakHost_SetRelayPowerEnable(bool enable);
HAL_StatusTypeDef GreenPakHost_ToggleAdcEnable(bool *newLevel);
HAL_StatusTypeDef GreenPakHost_SetAdcEnable(bool enable);
HAL_StatusTypeDef GreenPakHost_SetWdtAutoToggle(bool enable);
bool GreenPakHost_GetWdtAutoToggleEnabled(void);
bool GreenPakHost_GetWdtAutoToggleEnabledForDevice(GreenPakHost_Device_t device);
HAL_StatusTypeDef GreenPakHost_ServiceWdtAutoToggle(uint32_t nowMs);
void GreenPakHost_GetWdtDiagnostics(GreenPakHost_WdtDiagnostics_t *diag);
void GreenPakHost_ResetWdtDiagnostics(void);
HAL_StatusTypeDef GreenPakHost_ReadAnalogValues(GreenPakHost_AnalogValues_t *values);
HAL_StatusTypeDef GreenPakHost_ReadSignalReadback(uint8_t *value);
uint32_t GreenPakHost_GetLastError(void);

#endif /* GREENPAK_HOST_H */
