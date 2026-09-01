#include "greenpak_host.h"

#define GREENPAK_I2C_TIMEOUT_MS            50u
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

typedef struct
{
    uint8_t outputCache;
    bool outputCacheValid;
    bool wdtAutoToggleEnabled;
    bool wdtOutputLevel;
} GreenPakHost_DeviceState_t;

static I2C_HandleTypeDef *s_i2c = NULL;
static uint32_t s_lastError = 0u;
static GreenPakHost_Device_t s_selectedDevice = GREENPAK_DEVICE_08;
static GreenPakHost_DeviceState_t s_deviceState[GREENPAK_DEVICE_COUNT];

/* One common WDT schedule services every device that has WDT enabled. */
static uint32_t s_lastWdtToggleMs = 0u;
static uint32_t s_lastWdtToggleIntervalMs = 0u;
static uint32_t s_maxWdtToggleIntervalMs = 0u;
static uint32_t s_wdtServiceCallCount = 0u;
static uint32_t s_lastWdtServiceMs = 0u;
static uint32_t s_lastWdtServiceIntervalMs = 0u;
static uint32_t s_maxWdtServiceIntervalMs = 0u;
static uint32_t s_wdtToggleWriteCount = 0u;
static uint32_t s_wdtToggleWriteFailCount = 0u;

static bool isValidDevice(GreenPakHost_Device_t device)
{
    return ((uint32_t)device < (uint32_t)GREENPAK_DEVICE_COUNT);
}

static GreenPakHost_DeviceState_t *stateForDevice(GreenPakHost_Device_t device)
{
    if (!isValidDevice(device))
    {
        return NULL;
    }

    return &s_deviceState[(uint32_t)device];
}

uint8_t GreenPakHost_GetAddress7BitForDevice(GreenPakHost_Device_t device)
{
    return (device == GREENPAK_DEVICE_18) ?
           GREENPAK_I2C_ADDRESS_18_7BIT :
           GREENPAK_I2C_ADDRESS_08_7BIT;
}

uint16_t GreenPakHost_GetAddressHalForDevice(GreenPakHost_Device_t device)
{
    return (uint16_t)((uint16_t)GreenPakHost_GetAddress7BitForDevice(device) << 1);
}

GreenPakHost_Device_t GreenPakHost_GetSelectedDevice(void)
{
    return s_selectedDevice;
}

void GreenPakHost_SelectDevice(GreenPakHost_Device_t device)
{
    if (isValidDevice(device))
    {
        s_selectedDevice = device;
    }
}

void GreenPakHost_SelectNextDevice(void)
{
    s_selectedDevice = (s_selectedDevice == GREENPAK_DEVICE_08) ?
                       GREENPAK_DEVICE_18 :
                       GREENPAK_DEVICE_08;
}

uint8_t GreenPakHost_GetSelectedAddress7Bit(void)
{
    return GreenPakHost_GetAddress7BitForDevice(s_selectedDevice);
}

uint16_t GreenPakHost_GetSelectedAddressHal(void)
{
    return GreenPakHost_GetAddressHalForDevice(s_selectedDevice);
}

static bool bitIsHigh(uint8_t value, uint8_t bitIndex)
{
    return ((value & (uint8_t)(1u << bitIndex)) != 0u);
}

static bool anyWdtEnabled(void)
{
    for (uint32_t i = 0u; i < (uint32_t)GREENPAK_DEVICE_COUNT; i++)
    {
        if (s_deviceState[i].wdtAutoToggleEnabled)
        {
            return true;
        }
    }

    return false;
}

static bool anyOtherWdtEnabled(GreenPakHost_Device_t excludedDevice)
{
    for (uint32_t i = 0u; i < (uint32_t)GREENPAK_DEVICE_COUNT; i++)
    {
        if ((i != (uint32_t)excludedDevice) && s_deviceState[i].wdtAutoToggleEnabled)
        {
            return true;
        }
    }

    return false;
}

static HAL_StatusTypeDef readReg8ForDevice(GreenPakHost_Device_t device,
                                            uint16_t regAddr,
                                            uint8_t *value)
{
    if ((s_i2c == NULL) || (value == NULL) || !isValidDevice(device))
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(s_i2c,
                                            GreenPakHost_GetAddressHalForDevice(device),
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

static HAL_StatusTypeDef readReg16ForDevice(GreenPakHost_Device_t device,
                                             uint16_t regAddr,
                                             uint16_t *value)
{
    uint8_t rx[2] = {0u, 0u};

    if ((s_i2c == NULL) || (value == NULL) || !isValidDevice(device))
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(s_i2c,
                                            GreenPakHost_GetAddressHalForDevice(device),
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

static HAL_StatusTypeDef writeReg8ForDevice(GreenPakHost_Device_t device,
                                             uint16_t regAddr,
                                             uint8_t value)
{
    if ((s_i2c == NULL) || !isValidDevice(device))
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(s_i2c,
                                             GreenPakHost_GetAddressHalForDevice(device),
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

static HAL_StatusTypeDef readReg8(uint16_t regAddr, uint8_t *value)
{
    return readReg8ForDevice(s_selectedDevice, regAddr, value);
}

static HAL_StatusTypeDef readReg16(uint16_t regAddr, uint16_t *value)
{
    return readReg16ForDevice(s_selectedDevice, regAddr, value);
}

static HAL_StatusTypeDef setOutputForDevice(GreenPakHost_Device_t device,
                                             uint8_t outputIndex,
                                             bool high)
{
    if ((outputIndex >= 8u) || !isValidDevice(device))
    {
        return HAL_ERROR;
    }

    GreenPakHost_DeviceState_t *state = stateForDevice(device);
    uint8_t value = state->outputCache;

    if (!state->outputCacheValid)
    {
        HAL_StatusTypeDef st = readReg8ForDevice(device, GREENPAK_HOST_REG_OUTPUTS, &value);
        if (st != HAL_OK)
        {
            return st;
        }

        state->outputCache = value;
        state->outputCacheValid = true;
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

    HAL_StatusTypeDef st = writeReg8ForDevice(device, GREENPAK_HOST_REG_OUTPUTS, value);
    if (st == HAL_OK)
    {
        state->outputCache = value;
        state->outputCacheValid = true;

        if (outputIndex == GREENPAK_HOST_OUTPUT_WDT_INDEX)
        {
            state->wdtOutputLevel = high;
        }
    }

    return st;
}

void GreenPakHost_Init(I2C_HandleTypeDef *hi2c)
{
    s_i2c = hi2c;
    s_lastError = 0u;
    s_selectedDevice = GREENPAK_DEVICE_08;

    /*
     * Register 0x0061 is the host-output command register. Keep a separate
     * command cache for each supported GreenPAK so switching address does not
     * mix OUT0/OUT1/OUT2 state between the two devices.
     */
    for (uint32_t i = 0u; i < (uint32_t)GREENPAK_DEVICE_COUNT; i++)
    {
        s_deviceState[i].outputCache = 0u;
        s_deviceState[i].outputCacheValid = true;
        s_deviceState[i].wdtAutoToggleEnabled = false;
        s_deviceState[i].wdtOutputLevel = false;
    }

    s_lastWdtToggleMs = 0u;
    GreenPakHost_ResetWdtDiagnostics();
}

uint32_t GreenPakHost_GetLastError(void)
{
    return s_lastError;
}

HAL_StatusTypeDef GreenPakHost_ReadOutputs(GreenPakHost_OutputStatus_t *status)
{
    if (s_i2c == NULL)
    {
        return HAL_ERROR;
    }

    GreenPakHost_DeviceState_t *state = stateForDevice(s_selectedDevice);
    uint8_t value = state->outputCache;

    /*
     * Use IsDeviceReady only as a communication/ACK status check. The OUT0/OUT1/OUT2
     * ON/OFF values shown in the UI come from the selected device's local command cache.
     */
    HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(s_i2c,
                                                 GreenPakHost_GetSelectedAddressHal(),
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
        status->wdtAutoToggleEnabled = state->wdtAutoToggleEnabled;
    }

    return st;
}

HAL_StatusTypeDef GreenPakHost_SetOutput(uint8_t outputIndex, bool high)
{
    return setOutputForDevice(s_selectedDevice, outputIndex, high);
}

HAL_StatusTypeDef GreenPakHost_ToggleOutput(uint8_t outputIndex, bool *newLevel)
{
    if (outputIndex >= 8u)
    {
        return HAL_ERROR;
    }

    GreenPakHost_DeviceState_t *state = stateForDevice(s_selectedDevice);
    uint8_t value = state->outputCache;

    if (!state->outputCacheValid)
    {
        HAL_StatusTypeDef st = readReg8(GREENPAK_HOST_REG_OUTPUTS, &value);
        if (st != HAL_OK)
        {
            return st;
        }

        state->outputCache = value;
        state->outputCacheValid = true;
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
    GreenPakHost_Device_t device = s_selectedDevice;
    GreenPakHost_DeviceState_t *state = stateForDevice(device);
    bool otherDeviceEnabled = anyOtherWdtEnabled(device);

    /*
     * WDT enable belongs to the selected device, not to the address selector.
     * Always start/stop that device with OUT0 LOW. If another device already
     * has WDT enabled, leave the common WDT schedule untouched.
     */
    state->wdtAutoToggleEnabled = false;

    HAL_StatusTypeDef st = setOutputForDevice(device, GREENPAK_HOST_OUTPUT_WDT_INDEX, false);
    if (st != HAL_OK)
    {
        return st;
    }

    state->wdtOutputLevel = false;

    if (enable)
    {
        state->wdtAutoToggleEnabled = true;

        if (!otherDeviceEnabled)
        {
            s_lastWdtToggleMs = HAL_GetTick();
            GreenPakHost_ResetWdtDiagnostics();
        }
    }
    else if (!anyWdtEnabled())
    {
        s_lastWdtToggleMs = 0u;
    }

    return HAL_OK;
}

bool GreenPakHost_GetWdtAutoToggleEnabled(void)
{
    return GreenPakHost_GetWdtAutoToggleEnabledForDevice(s_selectedDevice);
}

bool GreenPakHost_GetWdtAutoToggleEnabledForDevice(GreenPakHost_Device_t device)
{
    GreenPakHost_DeviceState_t *state = stateForDevice(device);
    return (state != NULL) ? state->wdtAutoToggleEnabled : false;
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

    GreenPakHost_DeviceState_t *state = stateForDevice(s_selectedDevice);

    diag->autoToggleEnabled = state->wdtAutoToggleEnabled;
    diag->outputLevel = state->wdtOutputLevel;
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
    if (!anyWdtEnabled())
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

    HAL_StatusTypeDef result = HAL_OK;
    uint32_t firstFailureError = 0u;

    /*
     * Service enabled devices back-to-back on the same scheduler edge. Each
     * device tracks its own OUT0 level, so every scheduled write is a real
     * transition even if the second WDT was enabled later than the first.
     */
    for (uint32_t i = 0u; i < (uint32_t)GREENPAK_DEVICE_COUNT; i++)
    {
        GreenPakHost_DeviceState_t *state = &s_deviceState[i];
        if (!state->wdtAutoToggleEnabled)
        {
            continue;
        }

        GreenPakHost_Device_t device = (GreenPakHost_Device_t)i;
        bool nextLevel = !state->wdtOutputLevel;
        HAL_StatusTypeDef st = setOutputForDevice(device,
                                                  GREENPAK_HOST_OUTPUT_WDT_INDEX,
                                                  nextLevel);
        if (st == HAL_OK)
        {
            s_wdtToggleWriteCount++;
        }
        else
        {
            s_wdtToggleWriteFailCount++;
            if (result == HAL_OK)
            {
                result = st;
                firstFailureError = s_lastError;
            }
        }
    }

    /* Preserve a failure indication even if the second device then succeeds. */
    if (result != HAL_OK)
    {
        s_lastError = firstFailureError;
    }

    return result;
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
