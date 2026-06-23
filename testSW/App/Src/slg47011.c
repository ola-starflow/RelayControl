#include "slg47011.h"
#include "slg47011_config_data.h"

#include <stdio.h>
#include <stddef.h>

#define SLG47011_I2C_TIMEOUT_MS          50u
#define SLG47011_I2C_WRITE_CHUNK_SIZE    32u
#define SLG47011_I2C_READ_CHUNK_SIZE     32u
#define SLG47011_COMPARE_PRINT_LIMIT     32u
#define SLG47011_RAM_START_ADDR          0x0000u
#define SLG47011_RAM_END_ADDR            0x2249u
#define SLG47011_RAM_MAX_LEN             (SLG47011_RAM_END_ADDR + 1u)

/*
 * I2C/SPI configuration registers. Write these last so a new config cannot
 * change or disable the host interface in the middle of a RAM load.
 */
#define SLG47011_HOSTIF_CFG_START_ADDR   0x0064u
#define SLG47011_HOSTIF_CFG_END_ADDR     0x0066u

#define SLG47011_I2C_CONTROL_CODE        0x01u
#define SLG47011_I2C_ADDR_7BIT           (SLG47011_I2C_CONTROL_CODE << 3)
#define SLG47011_I2C_ADDR_HAL            (SLG47011_I2C_ADDR_7BIT << 1)

#define SLG47011_REG_HOST_OUTPUTS        0x0061u
#define SLG47011_REG_SIGNAL_READBACK     0x0062u
#define SLG47011_REG_DATABUF2_RESULT     0x2236u

#define SLG47011_TEMP_ADC_BITS           14u
#define SLG47011_TEMP_ADC_MAX            ((1u << SLG47011_TEMP_ADC_BITS) - 1u)
#define SLG47011_ADC_VREF_MV             1620.0f
#define SLG47011_TEMP_ADC_GAIN           1.0f
#define SLG47011_WDT_KICK_INTERVAL_MS     1000u

static I2C_HandleTypeDef *slgI2c = NULL;
static uint32_t lastI2cError = 0;
static uint8_t hostOutputCache = 0u;
static bool hostOutputCacheValid = false;
static bool wdtKicksEnabled = false;
static bool wdtKickLevel = false;
static uint32_t lastWdtKickTimeMs = 0u;

void SLG47011_Init(I2C_HandleTypeDef *hi2c)
{
    slgI2c = hi2c;
    lastI2cError = 0;
    hostOutputCache = 0u;
    hostOutputCacheValid = false;
    wdtKicksEnabled = false;
    wdtKickLevel = false;
    lastWdtKickTimeMs = 0u;
}

uint32_t SLG47011_GetLastError(void)
{
    return lastI2cError;
}

static HAL_StatusTypeDef readRegBytes(uint16_t regAddr, uint8_t *data, uint16_t len)
{
    if ((slgI2c == NULL) || (data == NULL) || (len == 0u))
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(slgI2c,
                                            SLG47011_I2C_ADDR_HAL,
                                            regAddr,
                                            I2C_MEMADD_SIZE_16BIT,
                                            data,
                                            len,
                                            SLG47011_I2C_TIMEOUT_MS);

    if (st != HAL_OK)
    {
        lastI2cError = HAL_I2C_GetError(slgI2c);
    }

    return st;
}


static HAL_StatusTypeDef writeRegBytes(uint16_t regAddr, const uint8_t *data, uint16_t len)
{
    if ((slgI2c == NULL) || (data == NULL) || (len == 0u))
    {
        return HAL_ERROR;
    }

    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(slgI2c,
                                             SLG47011_I2C_ADDR_HAL,
                                             regAddr,
                                             I2C_MEMADD_SIZE_16BIT,
                                             (uint8_t *)data,
                                             len,
                                             SLG47011_I2C_TIMEOUT_MS);

    if (st != HAL_OK)
    {
        lastI2cError = HAL_I2C_GetError(slgI2c);
    }

    return st;
}

static HAL_StatusTypeDef readReg8(uint16_t regAddr, uint8_t *value)
{
    return readRegBytes(regAddr, value, 1u);
}

static HAL_StatusTypeDef readReg16BE(uint16_t regAddr, uint16_t *value)
{
    uint8_t rx[2] = {0};

    HAL_StatusTypeDef st = readRegBytes(regAddr, rx, sizeof(rx));

    if (st == HAL_OK)
    {
        *value = ((uint16_t)rx[0] << 8) | rx[1];
    }

    return st;
}

static float tempRawToMilliVolt(uint16_t raw)
{
    uint32_t adcCode = raw & SLG47011_TEMP_ADC_MAX;

    float adcMv = ((float)adcCode * SLG47011_ADC_VREF_MV) /
                  (float)SLG47011_TEMP_ADC_MAX;

    return adcMv / SLG47011_TEMP_ADC_GAIN;
}

static float tempMilliVoltToDegC(float tempSensorMv)
{
    return (753.8f - tempSensorMv) / 1.83f;
}


static bool outputBitIsHigh(uint8_t raw, uint8_t outputIndex)
{
    return ((raw & (uint8_t)(1u << outputIndex)) != 0u);
}

static HAL_StatusTypeDef readHostOutputRegister(uint8_t *value)
{
    HAL_StatusTypeDef st = readReg8(SLG47011_REG_HOST_OUTPUTS, value);

    if (st == HAL_OK)
    {
        hostOutputCache = *value;
        hostOutputCacheValid = true;
        wdtKickLevel = outputBitIsHigh(hostOutputCache, SLG47011_HOST_OUTPUT_WDT_KICK);
    }

    return st;
}

HAL_StatusTypeDef SLG47011_ReadHostOutputs(SLG47011_HostOutputs_t *out)
{
    uint8_t value = 0u;
    HAL_StatusTypeDef st = readHostOutputRegister(&value);

    if (out != NULL)
    {
        out->ok = (st == HAL_OK);
        out->raw = value;
        out->wdtKickLevel = outputBitIsHigh(value, SLG47011_HOST_OUTPUT_WDT_KICK);
        out->relayPowerEnabled = outputBitIsHigh(value, SLG47011_HOST_OUTPUT_RELAY_POWER);
        out->adcShutdown = outputBitIsHigh(value, SLG47011_HOST_OUTPUT_ADC_SHUTDOWN);
        out->wdtKicksEnabled = wdtKicksEnabled;
    }

    return st;
}

HAL_StatusTypeDef SLG47011_SetHostOutput(uint8_t outputIndex, bool high)
{
    if (outputIndex >= 8u)
    {
        return HAL_ERROR;
    }

    uint8_t value = hostOutputCache;

    if (!hostOutputCacheValid)
    {
        HAL_StatusTypeDef st = readHostOutputRegister(&value);
        if (st != HAL_OK)
        {
            return st;
        }
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

    HAL_StatusTypeDef st = writeRegBytes(SLG47011_REG_HOST_OUTPUTS, &value, 1u);

    if (st == HAL_OK)
    {
        hostOutputCache = value;
        hostOutputCacheValid = true;

        if (outputIndex == SLG47011_HOST_OUTPUT_WDT_KICK)
        {
            wdtKickLevel = high;
        }
    }

    return st;
}

HAL_StatusTypeDef SLG47011_ToggleHostOutput(uint8_t outputIndex, bool *newState)
{
    if (outputIndex >= 8u)
    {
        return HAL_ERROR;
    }

    uint8_t value = hostOutputCache;

    if (!hostOutputCacheValid)
    {
        HAL_StatusTypeDef st = readHostOutputRegister(&value);
        if (st != HAL_OK)
        {
            return st;
        }
    }

    bool high = !outputBitIsHigh(value, outputIndex);
    HAL_StatusTypeDef st = SLG47011_SetHostOutput(outputIndex, high);

    if ((st == HAL_OK) && (newState != NULL))
    {
        *newState = high;
    }

    return st;
}

HAL_StatusTypeDef SLG47011_SetWdtKicksEnabled(bool enabled)
{
    wdtKicksEnabled = enabled;

    if (enabled)
    {
        lastWdtKickTimeMs = 0u;
    }

    return HAL_OK;
}

bool SLG47011_GetWdtKicksEnabled(void)
{
    return wdtKicksEnabled;
}

HAL_StatusTypeDef SLG47011_ServiceWdtKick(uint32_t nowMs)
{
    if (!wdtKicksEnabled)
    {
        return HAL_OK;
    }

    if ((lastWdtKickTimeMs != 0u) &&
        ((uint32_t)(nowMs - lastWdtKickTimeMs) < SLG47011_WDT_KICK_INTERVAL_MS))
    {
        return HAL_OK;
    }

    lastWdtKickTimeMs = nowMs;
    wdtKickLevel = !wdtKickLevel;

    return SLG47011_SetHostOutput(SLG47011_HOST_OUTPUT_WDT_KICK, wdtKickLevel);
}

HAL_StatusTypeDef SLG47011_ReadSignalReadback(SLG47011_SignalReadback_t *out)
{
    uint8_t value = 0;
    HAL_StatusTypeDef st = readReg8(SLG47011_REG_SIGNAL_READBACK, &value);

    if (out != NULL)
    {
        out->ok = (st == HAL_OK);
        out->raw = value;
    }

    return st;
}

HAL_StatusTypeDef SLG47011_ReadTemperature(SLG47011_Temperature_t *out)
{
    uint16_t raw = 0;
    HAL_StatusTypeDef st = readReg16BE(SLG47011_REG_DATABUF2_RESULT, &raw);

    if (out != NULL)
    {
        out->ok = (st == HAL_OK);

        if (st == HAL_OK)
        {
            out->raw = raw;
            out->sensorMv = tempRawToMilliVolt(raw);
            out->degC = tempMilliVoltToDegC(out->sensorMv);
        }
    }

    return st;
}

void SLG47011_PrintI2cScan(void)
{
    if (slgI2c == NULL)
    {
        printf("\r\nI2C scan skipped: SLG47011 not initialized\r\n");
        return;
    }

    printf("\r\nI2C scan:\r\n");

    for (uint8_t addr = 0; addr < 0x7F; addr++)
    {
        HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(slgI2c, addr << 1, 2, 20);

        if (st == HAL_OK)
        {
            printf("  Found device at 7-bit address 0x%02X\r\n", addr);
        }
    }

    printf("I2C scan done.\r\n\r\n");
}

void SLG47011_DumpDataBuffer2(void)
{
    printf("\r\nSLG47011 Data Buffer2 area:\r\n");

    for (uint16_t addr = 0x2226u; addr <= 0x2236u; addr += 2u)
    {
        uint8_t rx[2] = {0};
        HAL_StatusTypeDef st = readRegBytes(addr, rx, sizeof(rx));

        if (st == HAL_OK)
        {
            uint16_t be = ((uint16_t)rx[0] << 8) | rx[1];
            printf("  0x%04X : bytes %02X %02X   BE=%5u\r\n",
                   addr,
                   rx[0],
                   rx[1],
                   be);
        }
        else
        {
            printf("  0x%04X : FAIL err=0x%08lX\r\n", addr, lastI2cError);
        }
    }

    printf("\r\n");
}


static HAL_StatusTypeDef writeConfigRange(const uint8_t *config,
                                         size_t len,
                                         uint16_t startAddr,
                                         uint16_t endAddr)
{
    if (startAddr > endAddr)
    {
        return HAL_OK;
    }

    if ((size_t)startAddr >= len)
    {
        return HAL_OK;
    }

    if ((size_t)endAddr >= len)
    {
        endAddr = (uint16_t)(len - 1u);
    }

    uint16_t addr = startAddr;

    while (addr <= endAddr)
    {
        uint16_t remaining = (uint16_t)(endAddr - addr + 1u);
        uint16_t chunkLen = (remaining > SLG47011_I2C_WRITE_CHUNK_SIZE) ?
                            SLG47011_I2C_WRITE_CHUNK_SIZE :
                            remaining;

        HAL_StatusTypeDef st = writeRegBytes(addr, &config[addr], chunkLen);

        if (st != HAL_OK)
        {
            return st;
        }

        addr = (uint16_t)(addr + chunkLen);
    }

    return HAL_OK;
}

HAL_StatusTypeDef SLG47011_WriteRamConfig(const uint8_t *config, size_t len)
{
    if ((config == NULL) || (len == 0u))
    {
        return HAL_ERROR;
    }

    if (len > SLG47011_RAM_MAX_LEN)
    {
        return HAL_ERROR;
    }

    /*
     * Write RAM only. No OTP/NVM programming command is used here.
     *
     * The host interface config bytes are deliberately written last. If a new
     * image changes I2C_CONTROL_CODE_SEL or disables/reconfigures the host
     * interface, doing it early could break the remaining transfer.
     */
    HAL_StatusTypeDef st;

    st = writeConfigRange(config, len, 0x0000u, (SLG47011_HOSTIF_CFG_START_ADDR - 1u));
    if (st != HAL_OK)
    {
        return st;
    }

    st = writeConfigRange(config, len, (SLG47011_HOSTIF_CFG_END_ADDR + 1u), (uint16_t)(len - 1u));
    if (st != HAL_OK)
    {
        return st;
    }

    st = writeConfigRange(config, len, SLG47011_HOSTIF_CFG_START_ADDR, SLG47011_HOSTIF_CFG_END_ADDR);

    if (st == HAL_OK)
    {
        /* The RAM image may have changed 0x0061, so force the next host-output
         * operation to read the actual device state before modifying bits.
         */
        hostOutputCacheValid = false;
    }

    return st;
}

static bool slg47011_compare_addr(uint16_t addr)
{
    // Live/read-only host interface registers
    if (addr == 0x0062u) return false;                    // virtual output / live macrocell state
    if (addr == 0x00DEu) return false;                    // virtual output / live macrocell state
    if (addr == 0x00E0u) return false;                    // virtual output / live macrocell state
    if (addr >= 0x0169u && addr <= 0x0172u) return false;  // MathCore output
    if (addr >= 0x2200u && addr <= 0x2249u) return false;  // ADC/data buffer live content

    // Dynamic counter/current-value readback aliases seen in this design
    if (addr == 0x0032u) return false;
    if (addr == 0x0039u) return false;
    if (addr == 0x00DAu) return false;
    if (addr == 0x00DCu) return false;
    if (addr >= 0x00EEu && addr <= 0x013Cu) return false;

    // Status / protection / host-interface values seen changing on readback
    if (addr == 0x01CAu) return false;
    if (addr == 0x01CBu) return false;

    return true;
}

HAL_StatusTypeDef SLG47011_VerifyRamConfig(const uint8_t *config,
                                           size_t len,
                                           SLG47011_VerifyResult_t *result)
{
    if (result != NULL)
    {
        result->ok = false;
        result->failAddr = 0u;
        result->expected = 0u;
        result->actual = 0u;
    }

    if ((config == NULL) || (len == 0u) || (len > SLG47011_RAM_MAX_LEN))
    {
        return HAL_ERROR;
    }

    for (size_t offset = 0u; offset < len; offset++)
    {
        uint16_t addr = (uint16_t)(SLG47011_RAM_START_ADDR + offset);
        uint8_t actual = 0u;
        HAL_StatusTypeDef st = readReg8((uint16_t)(SLG47011_RAM_START_ADDR + offset), &actual);

        if (st != HAL_OK)
        {
            return st;
        }

        if (!slg47011_compare_addr(addr)) {
            continue;   // skip dynamic / read-only addresses
        }

        if (actual != config[offset])
        {
            if (result != NULL)
            {
                result->ok = false;
                result->failAddr = (uint16_t)(SLG47011_RAM_START_ADDR + offset);
                result->expected = config[offset];
                result->actual = actual;
            }

            return HAL_ERROR;
        }
    }

    if (result != NULL)
    {
        result->ok = true;
    }

    return HAL_OK;
}


HAL_StatusTypeDef SLG47011_CompareGeneratedRamConfig(SLG47011_RamCompareResult_t *result,
                                                     bool printMismatches)
{
    uint8_t rx[SLG47011_I2C_READ_CHUNK_SIZE];
    uint32_t printed = 0u;
    uint32_t mismatchCount = 0u;

    if (result != NULL)
    {
        result->ok = false;
        result->comparedBytes = 0u;
        result->mismatchCount = 0u;
        result->firstFailAddr = 0u;
        result->firstExpected = 0u;
        result->firstActual = 0u;
    }

    if ((slg47011_ram_config == NULL) ||
        (slg47011_ram_config_len == 0u) ||
        (slg47011_ram_config_len > SLG47011_RAM_MAX_LEN))
    {
        return HAL_ERROR;
    }

    printf("\r\nSLG47011 RAM compare against generated config:\r\n");
    printf("  Range : 0x%04X - 0x%04X\r\n",
           SLG47011_RAM_START_ADDR,
           (uint16_t)(SLG47011_RAM_START_ADDR + slg47011_ram_config_len - 1u));
    printf("  Bytes : %lu\r\n", (unsigned long)slg47011_ram_config_len);

    for (size_t offset = 0u; offset < slg47011_ram_config_len; )
    {
        size_t remaining = slg47011_ram_config_len - offset;
        uint16_t chunkLen = (remaining > SLG47011_I2C_READ_CHUNK_SIZE) ?
                            SLG47011_I2C_READ_CHUNK_SIZE :
                            (uint16_t)remaining;
        uint16_t addr = (uint16_t)(SLG47011_RAM_START_ADDR + offset);

       

        HAL_StatusTypeDef st = readRegBytes(addr, rx, chunkLen);

       

        if (st != HAL_OK)
        {
            printf("  Read failed at 0x%04X, err=0x%08lX\r\n", addr, lastI2cError);
            return st;
        }

        for (uint16_t i = 0u; i < chunkLen; i++)
        {
            uint8_t expected = slg47011_ram_config[offset + i];
            uint8_t actual = rx[i];

            if (!slg47011_compare_addr(addr + i))
            {
                continue;   // skip dynamic / read-only addresses
            }
            if (actual != expected)
            {
                if ((result != NULL) && (result->mismatchCount == 0u))
                {
                    result->firstFailAddr = (uint16_t)(addr + i);
                    result->firstExpected = expected;
                    result->firstActual = actual;
                }

                mismatchCount++;

                if (result != NULL)
                {
                    result->mismatchCount = mismatchCount;
                }

                if (printMismatches && (printed < SLG47011_COMPARE_PRINT_LIMIT))
                {
                    printf("  MISMATCH 0x%04X: expected 0x%02X, read 0x%02X\r\n",
                           (uint16_t)(addr + i),
                           expected,
                           actual);
                    printed++;
                }
            }
        }

        if (result != NULL)
        {
            result->comparedBytes += chunkLen;
        }

        offset += chunkLen;
    }

    if (mismatchCount == 0u)
    {
        if (result != NULL)
        {
            result->ok = true;
        }

        printf("  Result: MATCH\r\n\r\n");
        return HAL_OK;
    }

    if (result != NULL)
    {
        result->ok = false;
    }

    printf("  Result: %lu mismatch(es)", (unsigned long)mismatchCount);
    if (printMismatches && (mismatchCount > SLG47011_COMPARE_PRINT_LIMIT))
    {
        printf("; first %u shown", (unsigned int)SLG47011_COMPARE_PRINT_LIMIT);
    }
    printf("\r\n\r\n");

    return HAL_ERROR;
}

HAL_StatusTypeDef SLG47011_LoadGeneratedRamConfig(bool verifyAfterWrite,
                                                  SLG47011_VerifyResult_t *verifyResult)
{
    HAL_StatusTypeDef st = SLG47011_WriteRamConfig(slg47011_ram_config, slg47011_ram_config_len);

    if (st != HAL_OK)
    {
        return st;
    }

    if (verifyAfterWrite)
    {
        st = SLG47011_VerifyRamConfig(slg47011_ram_config,
                                      slg47011_ram_config_len,
                                      verifyResult);
    }

    return st;
}
