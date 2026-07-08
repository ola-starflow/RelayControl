#include "i2c_scan.h"

#include <stdio.h>

#define I2C_SCAN_TRIALS       2u
#define I2C_SCAN_TIMEOUT_MS   20u
#define GREENPAK_I2C_ADDRESS  0x08u

static I2C_HandleTypeDef *s_i2c = NULL;

void I2cScan_Init(I2C_HandleTypeDef *hi2c)
{
    s_i2c = hi2c;
}

HAL_StatusTypeDef I2cScan_Run(I2cScan_Result_t *result)
{
    if (result != NULL)
    {
        result->ackCount = 0u;
        result->firstAckAddress = 0u;
        result->hasAck = false;
    }

    if (s_i2c == NULL)
    {
        return HAL_ERROR;
    }

    printf("\r\nI2C ACK scan:\r\n");
    printf("  Scanning 7-bit addresses 0x00..0x7F at current I2C speed.\r\n");
    printf("  Expected configured GreenPAK address: 0x%02X\r\n\r\n", (unsigned int)GREENPAK_I2C_ADDRESS);

    uint32_t ackCount = 0u;
    uint8_t firstAck = 0u;

    for (uint16_t addr = 0u; addr <= 0x7Fu; addr++)
    {
        HAL_StatusTypeDef st = HAL_I2C_IsDeviceReady(s_i2c,
                                                     (uint16_t)(addr << 1),
                                                     I2C_SCAN_TRIALS,
                                                     I2C_SCAN_TIMEOUT_MS);
        if (st == HAL_OK)
        {
            if (ackCount == 0u)
            {
                firstAck = (uint8_t)addr;
            }

            ackCount++;
            printf("  ACK at 7-bit address 0x%02X", (unsigned int)addr);

            if (addr == GREENPAK_I2C_ADDRESS)
            {
                printf("  <-- configured GreenPAK address");
            }

            printf("\r\n");
        }
    }

    printf("\r\nI2C scan complete. ACK count: %lu\r\n", (unsigned long)ackCount);

    if (ackCount == 0u)
    {
        printf("  No I2C devices acknowledged. Check power, ground, pull-ups, SDA/SCL wiring, and host-interface enable.\r\n");
    }

    if (result != NULL)
    {
        result->ackCount = ackCount;
        result->firstAckAddress = firstAck;
        result->hasAck = (ackCount > 0u);
    }

    return HAL_OK;
}
