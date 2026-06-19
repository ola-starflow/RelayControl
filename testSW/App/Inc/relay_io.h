#ifndef RELAY_IO_H
#define RELAY_IO_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    RELAY_IO_REL_L = 0,
    RELAY_IO_REL_H,
    RELAY_IO_I2C_ADD,
    RELAY_IO_EN_H,
    RELAY_IO_EN_L,
    RELAY_IO_SHUTDOWN_N,
    RELAY_IO_RESET_N,
    RELAY_IO_COUNT
} RelayIoId_t;

typedef struct
{
    const char *name;
    bool isOutput;
    GPIO_PinState state;
} RelayIoState_t;

void RelayIo_Init(void);
bool RelayIo_Update(void);
void RelayIo_GetStates(RelayIoState_t *states, uint32_t maxCount);
GPIO_PinState RelayIo_ReadShutdownNLevel(void);
bool RelayIo_GetShutdownNHoldLowCommand(void);
GPIO_PinState RelayIo_ReadShutdownNLevel(void);

void RelayIo_ToggleResetN(void);
void RelayIo_ToggleShutdownN(void);
void RelayIo_ToggleEnH(void);
void RelayIo_ToggleEnL(void);

#endif /* RELAY_IO_H */
