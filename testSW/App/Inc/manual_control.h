#ifndef MANUAL_CONTROL_H
#define MANUAL_CONTROL_H

#include "stm32g4xx_hal.h"
#include <stdbool.h>

typedef struct
{
    GPIO_PinState resetN;
    GPIO_PinState shutdownNPin;
    bool shutdownMcuDrivesLow;
    bool shutdownPulledLowExternally;
    GPIO_PinState relEnH;
    GPIO_PinState relEnL;
    GPIO_PinState currentDirection;

    GPIO_PinState preChargeEn;
    GPIO_PinState relayL;
    GPIO_PinState relayH;
    GPIO_PinState boostPower;
    GPIO_PinState relayPwrEn;
} ManualControl_Status_t;

void ManualControl_Init(void);
void ManualControl_SetResetReleased(bool released);
void ManualControl_SetShutdownReleased(bool released);
void ManualControl_SetRelayEnableH(bool high);
void ManualControl_SetRelayEnableL(bool high);
void ManualControl_ToggleReset(void);
void ManualControl_ToggleShutdown(void);
void ManualControl_ToggleRelayEnableH(void);
void ManualControl_ToggleRelayEnableL(void);
void ManualControl_SetCurrentDirection(bool high);
void ManualControl_ToggleCurrentDirection(void);
void ManualControl_GetStatus(ManualControl_Status_t *status);
void ManualControl_PrintStatus(void);

#endif /* MANUAL_CONTROL_H */
