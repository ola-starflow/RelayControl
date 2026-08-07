#include "greenpak_host.h"

#define GREENPAK_I2C_TIMEOUT_MS            50u
#define GREENPAK_I2C_ADDR_HAL              (GREENPAK_I2C_ADDRESS_7BIT << 1)
#define GREENPAK_WDT_TOGGLE_INTERVAL_MS    200u

/*
 * Data Buffer result addresses.
 * Each Data Buffer stores 8 x 16-bit DATA words followed by the 16-bit
 * Result/moving-average word. Data Buffer2 result was previously verified at
 * 0x2236 in this project; DB0/DB1 use the corresponding earlier result slots.
 */
#define GREENPAK_DATABUF0_RESULT_ADDR       0x2212u
#define GREENPAK_DATABUF1_RESULT_ADDR       0x2224u
#define GREENPAK_DATABUF2_RESULT_ADDR       0x2236u

static I2C_HandleTypeDef *s_i2c = NULL;
static uint32_t s_lastError = 0u;
static uint8_t s_outputCache = 0u;
static bool s_outputCacheValid = false;
static bool s_wdtAutoToggleEnabled = false;
static bool s_wdtOutputLevel = false;
static uint32_t s_lastWdtToggleMs = 0u;
static uint32_t s_lastWdtToggleIntervalMs = 0u;
static uint32_t s_maxWdtToggleIntervalMs = 0u;
static uint32_t s_wdtServiceCallCount = 0u;
static uint32_t s_lastWdtServiceMs = 0u;
static uint32_t s_lastWdtServiceIntervalMs = 0u;
static uint32_t s_maxWdtServiceIntervalMs = 0u;
static uint32_t s_wdtToggleWriteCount = 0u;
static uint32_t s_wdtToggleWriteFailCount = 0u;

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

static HAL_StatusTypeDef readReg16(uint16_t regAddr, uint16_t *value)
{
    uint8_t rx[2] = {0u, 0u};

    if ((s_i2c == NULL) || (value == NULL))
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(s_i2c,
                                            GREENPAK_I2C_ADDR_HAL,
                                            regAddr,
                                            I2C_MEMADD_SIZE_16BIT,
                                            rx,
                                            2u,
                                            GREENPAK_I2C_TIMEOUT_MS);
    if (st != HAL_OK)
    {
        s_lastError = HAL_I2C_GetError(s_i2c);
    }
    else
    {
        s_lastError = 0u;
        *value = ((uint16_t)rx[0] << 8) | rx[1];
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
    GreenPakHost_ResetWdtDiagnostics();
}

uint32_t GreenPakHost_GetLastError(void)
{
    return s_lastError;
}

HAL_StatusTypeDef GreenPakHost_ReadOutputs(GreenPakHost_OutputStatus_t *status)
{
    uint8_t value = s_outputCache;

    /*
     * Use IsDeviceReady only as a communication/ACK status check. The OUT0/OUT1/OUT2
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
        status->adcOutputLevel = bitIsHigh(value, GREENPAK_HOST_OUTPUT_ADC_INDEX);
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

HAL_StatusTypeDef GreenPakHost_SetRelayPowerEnable(bool enable)
{
    return GreenPakHost_SetOutput(GREENPAK_HOST_OUTPUT_RELAY_PWR_INDEX, enable);
}

HAL_StatusTypeDef GreenPakHost_ToggleAdcEnable(bool *newLevel)
{
    return GreenPakHost_ToggleOutput(GREENPAK_HOST_OUTPUT_ADC_INDEX, newLevel);
}

HAL_StatusTypeDef GreenPakHost_SetAdcEnable(bool enable)
{
    return GreenPakHost_SetOutput(GREENPAK_HOST_OUTPUT_ADC_INDEX, enable);
}

HAL_StatusTypeDef GreenPakHost_SetWdtAutoToggle(bool enable)
{
    /*
     * Make manual WDT behavior deterministic:
     *   disabled -> OUT0 held LOW
     *   enabled  -> OUT0 starts LOW, then toggles every 400 ms
     */
    s_wdtAutoToggleEnabled = false;

    HAL_StatusTypeDef st = GreenPakHost_SetOutput(GREENPAK_HOST_OUTPUT_WDT_INDEX, false);
    if (st != HAL_OK)
    {
        return st;
    }

    s_wdtOutputLevel = false;
    s_lastWdtToggleMs = HAL_GetTick();
    GreenPakHost_ResetWdtDiagnostics();

    if (enable)
    {
        s_wdtAutoToggleEnabled = true;
    }

    return HAL_OK;
}

bool GreenPakHost_GetWdtAutoToggleEnabled(void)
{
    return s_wdtAutoToggleEnabled;
}

void GreenPakHost_ResetWdtDiagnostics(void)
{
    s_lastWdtToggleIntervalMs = 0u;
    s_maxWdtToggleIntervalMs = 0u;
    s_wdtServiceCallCount = 0u;
    s_lastWdtServiceMs = 0u;
    s_lastWdtServiceIntervalMs = 0u;
    s_maxWdtServiceIntervalMs = 0u;
    s_wdtToggleWriteCount = 0u;
    s_wdtToggleWriteFailCount = 0u;
}

void GreenPakHost_GetWdtDiagnostics(GreenPakHost_WdtDiagnostics_t *diag)
{
    if (diag == NULL)
    {
        return;
    }

    diag->autoToggleEnabled = s_wdtAutoToggleEnabled;
    diag->outputLevel = s_wdtOutputLevel;
    diag->targetToggleIntervalMs = GREENPAK_WDT_TOGGLE_INTERVAL_MS;
    diag->lastToggleMs = s_lastWdtToggleMs;
    diag->lastToggleIntervalMs = s_lastWdtToggleIntervalMs;
    diag->maxToggleIntervalMs = s_maxWdtToggleIntervalMs;
    diag->serviceCallCount = s_wdtServiceCallCount;
    diag->lastServiceIntervalMs = s_lastWdtServiceIntervalMs;
    diag->maxServiceIntervalMs = s_maxWdtServiceIntervalMs;
    diag->toggleWriteCount = s_wdtToggleWriteCount;
    diag->toggleWriteFailCount = s_wdtToggleWriteFailCount;
    diag->lastHalError = s_lastError;
}

HAL_StatusTypeDef GreenPakHost_ServiceWdtAutoToggle(uint32_t nowMs)
{
    if (!s_wdtAutoToggleEnabled)
    {
        return HAL_OK;
    }

    s_wdtServiceCallCount++;
    if (s_lastWdtServiceMs != 0u)
    {
        s_lastWdtServiceIntervalMs = (uint32_t)(nowMs - s_lastWdtServiceMs);
        if (s_lastWdtServiceIntervalMs > s_maxWdtServiceIntervalMs)
        {
            s_maxWdtServiceIntervalMs = s_lastWdtServiceIntervalMs;
        }
    }
    s_lastWdtServiceMs = nowMs;

    if ((s_lastWdtToggleMs != 0u) &&
        ((uint32_t)(nowMs - s_lastWdtToggleMs) < GREENPAK_WDT_TOGGLE_INTERVAL_MS))
    {
        return HAL_OK;
    }

    if (s_lastWdtToggleMs != 0u)
    {
        s_lastWdtToggleIntervalMs = (uint32_t)(nowMs - s_lastWdtToggleMs);
        if (s_lastWdtToggleIntervalMs > s_maxWdtToggleIntervalMs)
        {
            s_maxWdtToggleIntervalMs = s_lastWdtToggleIntervalMs;
        }
    }

    s_lastWdtToggleMs = nowMs;
    s_wdtOutputLevel = !s_wdtOutputLevel;

    HAL_StatusTypeDef st = GreenPakHost_SetOutput(GREENPAK_HOST_OUTPUT_WDT_INDEX, s_wdtOutputLevel);
    if (st == HAL_OK)
    {
        s_wdtToggleWriteCount++;
    }
    else
    {
        s_wdtToggleWriteFailCount++;
    }

    return st;
}

HAL_StatusTypeDef GreenPakHost_ReadSignalReadback(uint8_t *value)
{
    if (value == NULL)
    {
        return HAL_ERROR;
    }

    return readReg8(GREENPAK_HOST_REG_SIGNAL_READBACK, value);
}

HAL_StatusTypeDef GreenPakHost_ReadAnalogValues(GreenPakHost_AnalogValues_t *values)
{
    if (values == NULL)
    {
        return HAL_ERROR;
    }

    values->relayPowerContinuousRaw = 0u;
    values->relayPowerBoostRaw = 0u;
    values->temperatureRaw = 0u;
    values->status = HAL_OK;

    HAL_StatusTypeDef st = readReg16(GREENPAK_DATABUF0_RESULT_ADDR,
                                     &values->relayPowerContinuousRaw);
    if (st != HAL_OK)
    {
        values->status = st;
        return st;
    }

    st = readReg16(GREENPAK_DATABUF1_RESULT_ADDR,
                   &values->relayPowerBoostRaw);
    if (st != HAL_OK)
    {
        values->status = st;
        return st;
    }

    st = readReg16(GREENPAK_DATABUF2_RESULT_ADDR,
                   &values->temperatureRaw);
    values->status = st;
    return st;
}
