#include "greenpak_host.h"

#define GREENPAK_I2C_TIMEOUT_MS            50u
#define GREENPAK_I2C_ADDR_HAL              (GREENPAK_I2C_ADDRESS_7BIT << 1)
#define GREENPAK_WDT_TOGGLE_INTERVAL_MS    500u

static I2C_HandleTypeDef *s_i2c = NULL;
static uint32_t s_lastError = 0u;
static uint8_t s_outputCache = 0u;
static bool s_outputCacheValid = false;
static bool s_wdtAutoToggleEnabled = false;
static bool s_wdtOutputLevel = false;
static uint32_t s_lastWdtToggleMs = 0u;

static bool bitIsHigh(uint8_t value, uint8_t bitIndex)
{
    return ((value & (uint8_t)(1u << bitIndex)) != 0u);
}

static HAL_StatusTypeDef readReg8(uint16_t regAddr, uint8_t *value)
{
    if ((s_i2c == NULL) || (value == NULL))
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(s_i2c,
                                            GREENPAK_I2C_ADDR_HAL,
                                            regAddr,
                                            I2C_MEMADD_SIZE_16BIT,
                                            value,
                                            1u,
                                            GREENPAK_I2C_TIMEOUT_MS);
    if (st != HAL_OK)
    {
        s_lastError = HAL_I2C_GetError(s_i2c);
    }
    else
    {
        s_lastError = 0u;
    }

    return st;
}

static HAL_StatusTypeDef writeReg8(uint16_t regAddr, uint8_t value)
{
    if (s_i2c == NULL)
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(s_i2c,
                                             GREENPAK_I2C_ADDR_HAL,
                                             regAddr,
                                             I2C_MEMADD_SIZE_16BIT,
                                             &value,
                                             1u,
                                             GREENPAK_I2C_TIMEOUT_MS);
    if (st != HAL_OK)
    {
        s_lastError = HAL_I2C_GetError(s_i2c);
    }
    else
    {
        s_lastError = 0u;
    }

    return st;
}

void GreenPakHost_Init(I2C_HandleTypeDef *hi2c)
{
    s_i2c = hi2c;
    s_lastError = 0u;
    /*
     * Register 0x0061 is the host-output command register. For the live panel
     * we report the command state that this MCU has written, instead of relying
     * on reading the register back. This keeps OUT0/OUT1 status deterministic
     * even if the device readback behavior is not a true output latch readback.
     */
    s_outputCache = 0u;
    s_outputCacheValid = true;
    s_wdtAutoToggleEnabled = false;
    s_wdtOutputLevel = false;
    s_lastWdtToggleMs = 0u;
}

uint32_t GreenPakHost_GetLastError(void)
{
    return s_lastError;
}

HAL_StatusTypeDef GreenPakHost_ReadOutputs(GreenPakHost_OutputStatus_t *status)
{
    uint8_t value = s_outputCache;

    /*
     * Use IsDeviceReady only as a communication/ACK status check. The OUT0/OUT1
     * ON/OFF values shown in the UI come from the local command cache.
     */
    HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(s_i2c,
                                                 GREENPAK_I2C_ADDR_HAL,
                                                 1u,
                                                 GREENPAK_I2C_TIMEOUT_MS);
    if (st != HAL_OK)
    {
        s_lastError = HAL_I2C_GetError(s_i2c);
    }
    else
    {
        s_lastError = 0u;
    }

    if (status != NULL)
    {
        status->ok = (st == HAL_OK);
        status->raw = value;
        status->wdtOutputLevel = bitIsHigh(value, GREENPAK_HOST_OUTPUT_WDT_INDEX);
        status->relayPwrOutputLevel = bitIsHigh(value, GREENPAK_HOST_OUTPUT_RELAY_PWR_INDEX);
        status->wdtAutoToggleEnabled = s_wdtAutoToggleEnabled;
    }

    return st;
}

HAL_StatusTypeDef GreenPakHost_SetOutput(uint8_t outputIndex, bool high)
{
    if (outputIndex >= 8u)
    {
        return HAL_ERROR;
    }

    uint8_t value = s_outputCache;

    if (!s_outputCacheValid)
    {
        HAL_StatusTypeDef st = readReg8(GREENPAK_HOST_REG_OUTPUTS, &value);
        if (st != HAL_OK)
        {
            return st;
        }

        s_outputCache = value;
        s_outputCacheValid = true;
    }

    uint8_t mask = (uint8_t)(1u << outputIndex);
    if (high)
    {
        value |= mask;
    }
    else
    {
        value &= (uint8_t)~mask;
    }

    HAL_StatusTypeDef st = writeReg8(GREENPAK_HOST_REG_OUTPUTS, value);
    if (st == HAL_OK)
    {
        s_outputCache = value;
        s_outputCacheValid = true;

        if (outputIndex == GREENPAK_HOST_OUTPUT_WDT_INDEX)
        {
            s_wdtOutputLevel = high;
        }
    }

    return st;
}

HAL_StatusTypeDef GreenPakHost_ToggleOutput(uint8_t outputIndex, bool *newLevel)
{
    if (outputIndex >= 8u)
    {
        return HAL_ERROR;
    }

    uint8_t value = s_outputCache;

    if (!s_outputCacheValid)
    {
        HAL_StatusTypeDef st = readReg8(GREENPAK_HOST_REG_OUTPUTS, &value);
        if (st != HAL_OK)
        {
            return st;
        }

        s_outputCache = value;
        s_outputCacheValid = true;
    }

    bool next = !bitIsHigh(value, outputIndex);
    HAL_StatusTypeDef st = GreenPakHost_SetOutput(outputIndex, next);

    if ((st == HAL_OK) && (newLevel != NULL))
    {
        *newLevel = next;
    }

    return st;
}

HAL_StatusTypeDef GreenPakHost_ToggleRelayPowerEnable(bool *newLevel)
{
    return GreenPakHost_ToggleOutput(GREENPAK_HOST_OUTPUT_RELAY_PWR_INDEX, newLevel);
}

HAL_StatusTypeDef GreenPakHost_SetWdtAutoToggle(bool enable)
{
    s_wdtAutoToggleEnabled = enable;

    if (enable)
    {
        s_lastWdtToggleMs = 0u;
        s_wdtOutputLevel = bitIsHigh(s_outputCache, GREENPAK_HOST_OUTPUT_WDT_INDEX);
    }

    return HAL_OK;
}

bool GreenPakHost_GetWdtAutoToggleEnabled(void)
{
    return s_wdtAutoToggleEnabled;
}

HAL_StatusTypeDef GreenPakHost_ServiceWdtAutoToggle(uint32_t nowMs)
{
    if (!s_wdtAutoToggleEnabled)
    {
        return HAL_OK;
    }

    if ((s_lastWdtToggleMs != 0u) &&
        ((uint32_t)(nowMs - s_lastWdtToggleMs) < GREENPAK_WDT_TOGGLE_INTERVAL_MS))
    {
        return HAL_OK;
    }

    s_lastWdtToggleMs = nowMs;
    s_wdtOutputLevel = !s_wdtOutputLevel;

    return GreenPakHost_SetOutput(GREENPAK_HOST_OUTPUT_WDT_INDEX, s_wdtOutputLevel);
}
