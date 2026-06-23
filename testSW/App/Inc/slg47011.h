#ifndef SLG47011_H
#define SLG47011_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct
{
    bool ok;
    uint8_t raw;
} SLG47011_SignalReadback_t;

typedef struct
{
    bool ok;
    uint16_t raw;
    float sensorMv;
    float degC;
} SLG47011_Temperature_t;

#define SLG47011_HOST_OUTPUT_WDT_KICK        0u
#define SLG47011_HOST_OUTPUT_RELAY_POWER     1u
#define SLG47011_HOST_OUTPUT_ADC_SHUTDOWN    2u

typedef struct
{
    bool ok;
    uint8_t raw;
    bool wdtKickLevel;
    bool relayPowerEnabled;
    bool adcShutdown;
    bool wdtKicksEnabled;
} SLG47011_HostOutputs_t;

typedef struct
{
    bool ok;
    uint16_t failAddr;
    uint8_t expected;
    uint8_t actual;
} SLG47011_VerifyResult_t;

typedef struct
{
    bool ok;
    size_t comparedBytes;
    uint32_t mismatchCount;
    uint16_t firstFailAddr;
    uint8_t firstExpected;
    uint8_t firstActual;
} SLG47011_RamCompareResult_t;

void SLG47011_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef SLG47011_ReadSignalReadback(SLG47011_SignalReadback_t *out);
HAL_StatusTypeDef SLG47011_ReadTemperature(SLG47011_Temperature_t *out);

HAL_StatusTypeDef SLG47011_ReadHostOutputs(SLG47011_HostOutputs_t *out);
HAL_StatusTypeDef SLG47011_SetHostOutput(uint8_t outputIndex, bool high);
HAL_StatusTypeDef SLG47011_ToggleHostOutput(uint8_t outputIndex, bool *newState);
HAL_StatusTypeDef SLG47011_SetWdtKicksEnabled(bool enabled);
bool SLG47011_GetWdtKicksEnabled(void);
HAL_StatusTypeDef SLG47011_ServiceWdtKick(uint32_t nowMs);

/*
 * Write a complete volatile RAM configuration image.
 * This uses ordinary I2C writes to RAM/register address space only.
 * It does not execute any OTP/NVM program command.
 */
HAL_StatusTypeDef SLG47011_WriteRamConfig(const uint8_t *config, size_t len);
HAL_StatusTypeDef SLG47011_VerifyRamConfig(const uint8_t *config,
                                           size_t len,
                                           SLG47011_VerifyResult_t *result);
HAL_StatusTypeDef SLG47011_LoadGeneratedRamConfig(bool verifyAfterWrite,
                                                  SLG47011_VerifyResult_t *verifyResult);

/*
 * Read back the generated RAM config range and compare against
 * slg47011_ram_config[]. This does not write anything.
 */
HAL_StatusTypeDef SLG47011_CompareGeneratedRamConfig(SLG47011_RamCompareResult_t *result,
                                                     bool printMismatches);

uint32_t SLG47011_GetLastError(void);
void SLG47011_PrintI2cScan(void);
void SLG47011_DumpDataBuffer2(void);

#endif /* SLG47011_H */
