#include "appMain.h"
#include "main.h"


#include <stdio.h>
#include <sys/unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "sSerial.h"


//serial handle and buffers for debug output
static uint8_t debugRxBuffer[128];
static uint8_t debugTxBuffer[4096];
static SSerial_HandleTypeDef debugSerialHandle __attribute__((aligned(8)));

static uint8_t slgSignalReadback = 0;
static uint8_t lastSlgSignalReadback = 0;
static bool slgReadOk = false;

extern I2C_HandleTypeDef hi2c1;

static bool slgTempOk = false;
static uint16_t slgTempRaw = 0;
static float slgTempMv = 0.0f;
static float slgTempDegC = 0.0f;
static bool displayDirty = false;

static uint16_t lastSlgTempRaw = 0;

//printf redirection to sSerial
int _write(int file, char *ptr, int len)
{
    (void)file;
    if (ptr == NULL || len <= 0) { return 0;}
    sSerial_tx(&debugSerialHandle, (uint8_t *)ptr, (uint16_t)len);    
    return len; //only used for debug,do not care about actual number of bytes written
}


typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *name;
    bool isOutput;
} RelayPin_t;

static const RelayPin_t relayPins[] =
{
    { REL_L_GPIO_Port,             REL_L_Pin,             "REL_L input",          false },
    { REL_H_GPIO_Port,             REL_H_Pin,             "REL_H input",          false },

    { REL_I2C_ADD_GPIO_Port,       REL_I2C_ADD_Pin,       "REL_I2C_ADD output",   true  },
    { REL_EN_H_GPIO_Port,          REL_EN_H_Pin,          "REL_EN_H output",      true  },
    { REL_EN_L_GPIO_Port,          REL_EN_L_Pin,          "REL_EN_L output",      true  },
    { REL_SHUTDOWN_N_GPIO_Port,    REL_SHUTDOWN_N_Pin,    "REL_SHUTDOWN_N output",true  },
    { REL_RESET_N_GPIO_Port,       REL_RESET_N_Pin,       "REL_RESET_N output",   true  },
};

#define SLG47011_I2C_TIMEOUT_MS          50u

#define SLG47011_I2C_CONTROL_CODE        0x01u
#define SLG47011_I2C_ADDR_7BIT           (SLG47011_I2C_CONTROL_CODE << 3)
#define SLG47011_I2C_ADDR_HAL            (SLG47011_I2C_ADDR_7BIT << 1)




/*
 * Data Buffer2:
 * Use RESULT first. If your GreenPAK design uses raw storage data instead,
 * try SLG47011_REG_DATABUF2_DATA0 instead.
 */
#define SLG47011_REG_DATABUF2_DATA0      0x2220u
#define SLG47011_REG_DATABUF2_RESULT     0x2236u

#define SLG47011_TEMP_ADC_BITS           14u
#define SLG47011_TEMP_ADC_MAX            ((1u << SLG47011_TEMP_ADC_BITS) - 1u)

/*
 * Adjust this to match the ADC reference used in your GreenPAK design.
 * Common examples might be 1200 mV, 1800 mV, 2500 mV, or VDD = 3300 mV.
 */
#define SLG47011_ADC_VREF_MV             1620.0f

/*
 * Adjust if PGA/gain or scaling is used before the ADC.
 * If no gain/scaling: 1.0f
 */
#define SLG47011_TEMP_ADC_GAIN           1.0f

#define RELAY_PIN_COUNT   (sizeof(relayPins) / sizeof(relayPins[0]))

static GPIO_PinState lastPinState[RELAY_PIN_COUNT];

static GPIO_PinState readRelayPin(uint32_t index)
{
    return HAL_GPIO_ReadPin(relayPins[index].port, relayPins[index].pin);
}

static HAL_StatusTypeDef SLG47011_ReadRegBytes(uint16_t regAddr,
                                               uint8_t *data,
                                               uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1,
                            SLG47011_I2C_ADDR_HAL,
                            regAddr,
                            I2C_MEMADD_SIZE_16BIT,
                            data,
                            len,
                            SLG47011_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef SLG47011_ReadReg16(uint16_t regAddr, uint16_t *value)
{
    uint8_t rx[2];

    HAL_StatusTypeDef st = SLG47011_ReadRegBytes(regAddr, rx, 2);

    if (st == HAL_OK)
    {       
        *value = ((uint16_t)rx[0] << 8) | rx[1];       
    }

    return st;
}

static HAL_StatusTypeDef SLG47011_ReadDataBuffer2Raw(uint16_t *raw)
{
    return SLG47011_ReadReg16(SLG47011_REG_DATABUF2_RESULT, raw);

    /*
     * If your design stores the value in Data Buffer2 DATA[0] instead of Result,
     * use this instead:
     */
     // return SLG47011_ReadReg16(SLG47011_REG_DATABUF2_DATA0, raw);
     
}

static float SLG47011_TempRawToMilliVolt(uint16_t raw)
{
    /*
     * Mask to configured ADC resolution.
     * For 14-bit ADC, valid raw is 0..16383.
     */
    uint32_t adcCode = raw & SLG47011_TEMP_ADC_MAX;

    float adcMv = ((float)adcCode * SLG47011_ADC_VREF_MV) /
                  (float)SLG47011_TEMP_ADC_MAX;

    /*
     * Undo PGA/scaling if used.
     */
    return adcMv / SLG47011_TEMP_ADC_GAIN;
}

static float SLG47011_TempMilliVoltToDegC(float tempSensorMv)
{
    /*
     * Datasheet: VTS_OUT[mV] = -1.83 * T[degC] + 753.8
     * Therefore: T = (753.8 - VTS_OUT) / 1.83
     */
    return (753.8f - tempSensorMv) / 1.83f;
}

static HAL_StatusTypeDef SLG47011_ReadTemperature(float *tempDegC,
                                                  uint16_t *rawOut,
                                                  float *mvOut)
{
    uint16_t raw = 0;

    HAL_StatusTypeDef st = SLG47011_ReadDataBuffer2Raw(&raw);

    if (st == HAL_OK)
    {
        float mv = SLG47011_TempRawToMilliVolt(raw);
        float degC = SLG47011_TempMilliVoltToDegC(mv);

        if (rawOut != NULL)
        {
            *rawOut = raw;
        }

        if (mvOut != NULL)
        {
            *mvOut = mv;
        }

        if (tempDegC != NULL)
        {
            *tempDegC = degC;
        }
    }

    return st;
}

static void captureRelayPinStates(GPIO_PinState *stateBuffer)
{
    for (uint32_t i = 0; i < RELAY_PIN_COUNT; i++)
    {
        stateBuffer[i] = readRelayPin(i);
    }
}

static bool relayPinStateChanged(void)
{
    bool changed = false;

    for (uint32_t i = 0; i < RELAY_PIN_COUNT; i++)
    {
        GPIO_PinState currentState = readRelayPin(i);

        if (currentState != lastPinState[i])
        {
            changed = true;
            lastPinState[i] = currentState;
        }
    }

    return changed;
}

static const char *bitText(uint8_t value, uint8_t bit)
{
    return (value & (1u << bit)) ? "HIGH" : "LOW";
}

static void printRelayPinStates(void)
{
    printf("\033[2J");      // Clear terminal screen
    printf("\033[H");       // Move cursor to top-left
    
    printf("========================================\r\n");
    printf("        Relay test firmware status       \r\n");
    printf("========================================\r\n\r\n");

    printf("Commands:\r\n");
    printf("  r : toggle REL_RESET_N\r\n");
    printf("  s : toggle REL_SHUTDOWN_N\r\n");
    printf("  h : toggle REL_EN_H\r\n");
    printf("  l : toggle REL_EN_L\r\n");
    printf("\r\n");

    printf("outputs:\r\n");
    printf("  %-20s : %s\r\n",
           "REL_H",
           HAL_GPIO_ReadPin(REL_H_GPIO_Port, REL_H_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW");

    printf("  %-20s : %s\r\n",
           "REL_L",
           HAL_GPIO_ReadPin(REL_L_GPIO_Port, REL_L_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW");

    printf("Inputs:\r\n");
    printf("  %-20s : %s\r\n",
           "REL_RESET_N",
           HAL_GPIO_ReadPin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW");

    printf("  %-20s : %s\r\n\r\n",
           "REL_SHUTDOWN_N",
           HAL_GPIO_ReadPin(REL_SHUTDOWN_N_GPIO_Port, REL_SHUTDOWN_N_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW");

    printf("  %-20s : %s\r\n",
           "REL_EN_H",
           HAL_GPIO_ReadPin(REL_EN_H_GPIO_Port, REL_EN_H_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW");

    printf("  %-20s : %s\r\n",
           "REL_EN_L",
           HAL_GPIO_ReadPin(REL_EN_L_GPIO_Port, REL_EN_L_Pin) == GPIO_PIN_SET ? "HIGH" : "LOW");
  

    printf("\r\n");

    printf("\r\nSLG47011 Signal Readback:\r\n");

if (slgReadOk)
{
    printf("  Raw byte             : 0x%02X\r\n", slgSignalReadback);
    printf("  Virtual Output 0     : %s\r\n", bitText(slgSignalReadback, 0));
    printf("  Virtual Output 1     : %s\r\n", bitText(slgSignalReadback, 1));
    printf("  Virtual Output 2     : %s\r\n", bitText(slgSignalReadback, 2));
    printf("  Virtual Output 3     : %s\r\n", bitText(slgSignalReadback, 3));
    printf("  Virtual Output 4     : %s\r\n", bitText(slgSignalReadback, 4));
    printf("  Virtual Output 5     : %s\r\n", bitText(slgSignalReadback, 5));
    printf("  Virtual Output 6     : %s\r\n", bitText(slgSignalReadback, 6));
    printf("  Virtual Output 7     : %s\r\n", bitText(slgSignalReadback, 7));



    
}
else
{
    printf("  I2C read failed\r\n");
}

printf("\r\nSLG47011 Temperature:\r\n");

if (slgTempOk)
{
    int32_t mv_x10 = (int32_t)(slgTempMv * 10.0f);
    int32_t temp_x10 = (int32_t)(slgTempDegC * 10.0f);

    printf("  Data Buffer2 raw    : 0x%04X / %u\r\n", slgTempRaw, slgTempRaw);
    printf("  TS voltage          : %ld.%01ld mV\r\n",
           mv_x10 / 10,
           labs(mv_x10 % 10));

    printf("  Temperature         : %ld.%01ld degC\r\n",
           temp_x10 / 10,
           labs(temp_x10 % 10));
}
else
{
    printf("  Temperature read    : I2C read failed\r\n");
}
    
}

static void SLG47011_DumpDataBufferArea(void)
{
    printf("\r\nSLG47011 Data Buffer area:\r\n");

    for (uint16_t addr = 0x2226; addr <= 0x2236; addr += 2)
    {
        uint8_t rx[2] = {0};

        HAL_StatusTypeDef st = SLG47011_ReadRegBytes(addr, rx, 2);

        if (st == HAL_OK)
        {
            uint16_t le = ((uint16_t)rx[1] << 8) | rx[0];
            uint16_t be = ((uint16_t)rx[0] << 8) | rx[1];

            printf("  0x%04X : bytes %02X %02X   LE=%5u   BE=%5u\r\n",
                   addr,
                   rx[0],
                   rx[1],
                   le,
                   be);
        }
        else
        {
            printf("  0x%04X : FAIL err=0x%08lX\r\n",
                   addr,
                   HAL_I2C_GetError(&hi2c1));
        }
    }

    
     for(int i = 0; i < 200; i++)
            {
                HAL_Delay(1); //small delay to ensure the output is sent before any potential new output that can come from the main loop
                sSerial_update(&debugSerialHandle);
            }
            printf("\r\n");
}

static bool handleRelayCommand(uint8_t rxData)
{
    bool commandHandled = true;

    switch (rxData)
    {
        case 'r':
        case 'R':
            HAL_GPIO_TogglePin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin);
            break;

        case 's':
        case 'S':
            HAL_GPIO_TogglePin(REL_SHUTDOWN_N_GPIO_Port, REL_SHUTDOWN_N_Pin);
            break;

        case 'h':
        case 'H':
            HAL_GPIO_TogglePin(REL_EN_H_GPIO_Port, REL_EN_H_Pin);
            break;

        case 'l':
        case 'L':
            HAL_GPIO_TogglePin(REL_EN_L_GPIO_Port, REL_EN_L_Pin);
            break;
        case 'd':
        case 'D':
            SLG47011_DumpDataBufferArea();
            break;

        default:
            commandHandled = false;
            break;
    }

    return commandHandled;
}

#define SLG47011_I2C_TIMEOUT_MS          50u

/*
 * Set this to match your GreenPAK project.
 *
 * SLG47011 7-bit I2C address = I2C_CONTROL_CODE << 3
 *
 * Example:
 *   Control code 0b0001 -> 7-bit address 0x08
 */

#define SLG47011_I2C_ADDR_HAL            (SLG47011_I2C_ADDR_7BIT << 1)

/*
 * Signal Readback / I2C-SPI Virtual Output register.
 */
#define SLG47011_REG_SIGNAL_READBACK     0x0062u

static HAL_StatusTypeDef SLG47011_ReadReg8(uint16_t regAddr, uint8_t *value)
{
    return HAL_I2C_Mem_Read(&hi2c1,
                            SLG47011_I2C_ADDR_HAL,
                            regAddr,
                            I2C_MEMADD_SIZE_16BIT,
                            value,
                            1,
                            SLG47011_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef SLG47011_ReadSignalReadback(uint8_t *readback)
{
    return SLG47011_ReadReg8(SLG47011_REG_SIGNAL_READBACK, readback);
}

static void printI2cError(void)
{
    uint32_t err = HAL_I2C_GetError(&hi2c1);

    printf("I2C error: 0x%08lX\r\n", err);

    if (err & HAL_I2C_ERROR_BERR)    printf("  BERR  - bus error\r\n");
    if (err & HAL_I2C_ERROR_ARLO)    printf("  ARLO  - arbitration lost\r\n");
    if (err & HAL_I2C_ERROR_AF)      printf("  AF    - acknowledge failure / NACK\r\n");
    if (err & HAL_I2C_ERROR_OVR)     printf("  OVR   - overrun/underrun\r\n");
    if (err & HAL_I2C_ERROR_DMA)     printf("  DMA   - DMA transfer error\r\n");
    if (err & HAL_I2C_ERROR_TIMEOUT) printf("  TIMEOUT\r\n");
}

static bool SLG47011_UpdateTemperature(void)
{
    uint16_t raw = 0;
    float mv = 0.0f;
    float degC = 0.0f;

    HAL_StatusTypeDef st = SLG47011_ReadTemperature(&degC, &raw, &mv);

    if (st == HAL_OK)
    {
        bool changed = false;

        slgTempOk = true;
        slgTempRaw = raw;
        slgTempMv = mv;
        slgTempDegC = degC;

        if (raw != lastSlgTempRaw)
        {
            lastSlgTempRaw = raw;
            changed = true;
        }

        return changed;
    }
    else
    {
        bool changed = slgTempOk;
        slgTempOk = false;
        return changed;
    }
}

static void I2C_Scan(void)
{
    printf("\r\nI2C scan:\r\n");

    for (uint8_t addr = 1; addr < 0x7F; addr++)
    {
        HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(&hi2c1, addr << 1, 2, 20);
        if (st == HAL_OK)
        {
            printf("  Found device at 7-bit address 0x%02X\r\n", addr);
        }
        
    }

    printf("I2C scan done.\r\n\r\n");
}



void App_Init(void)
{
    //setup debug serial port    
    debugSerialHandle.uartBase = LPUART1;
    debugSerialHandle.baudrate = 115200;
    debugSerialHandle.pRxBuffPtr = debugRxBuffer;
    debugSerialHandle.pTxBuffPtr = debugTxBuffer;
    debugSerialHandle.RxBufferSize = sizeof(debugRxBuffer);
    debugSerialHandle.TxBufferSize = sizeof(debugTxBuffer);
    debugSerialHandle.dmaNumber = DMA1;
	debugSerialHandle.txDmaChannel = DMA1_Channel2;	
	debugSerialHandle.rxDmaChannel = DMA1_Channel1;
    sSerial_init(&debugSerialHandle);   

    captureRelayPinStates(lastPinState);
    printRelayPinStates();
    I2C_Scan();
  
}

static bool SLG47011_UpdateSignalReadback(void)
{
    uint8_t value = 0;

    HAL_StatusTypeDef status = SLG47011_ReadSignalReadback(&value);

    if (status == HAL_OK)
    {
        slgReadOk = true;

        if (value != lastSlgSignalReadback)
        {
            slgSignalReadback = value;
            lastSlgSignalReadback = value;
            return true;
        }

        slgSignalReadback = value;
        return false;
    }
    else
    {
        bool changed = slgReadOk;
        slgReadOk = false;
        return changed;
    }
}




#define SLG47011_POLL_INTERVAL_MS  200u

uint32_t lastSlgPollTime = 0;

void App_Run(void)
{
    uint8_t rxData = 0;
    while (1)
    {
        bool shouldPrintState = false;

        if (sSerial_rx(&debugSerialHandle, &rxData))
        {
            if (handleRelayCommand(rxData))
            {
                shouldPrintState = true;
            }
        }

        if (relayPinStateChanged())
        {
            shouldPrintState = true;
        }

        if (shouldPrintState)
        {
            captureRelayPinStates(lastPinState);
            printRelayPinStates();
            for(int i = 0; i < 200; i++)
            {
                HAL_Delay(1); //small delay to ensure the output is sent before any potential new output that can come from the main loop
                sSerial_update(&debugSerialHandle);
            }
        }

        uint32_t now = HAL_GetTick();

        if ((now - lastSlgPollTime) >= SLG47011_POLL_INTERVAL_MS)
        {
            lastSlgPollTime = now;

            if (SLG47011_UpdateTemperature())
            {
                displayDirty = true;
            }
            if (SLG47011_UpdateSignalReadback())
            {
                shouldPrintState = true;
            }
        }



         sSerial_update(&debugSerialHandle);       

    }

}