#ifndef I2C_SCAN_H
#define I2C_SCAN_H

#include "stm32g4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    uint32_t ackCount;
    uint8_t firstAckAddress;
    bool hasAck;
} I2cScan_Result_t;

void I2cScan_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef I2cScan_Run(I2cScan_Result_t *result);

#endif /* I2C_SCAN_H */
