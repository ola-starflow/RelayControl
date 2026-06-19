#include "relay_io.h"
#include "main.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *name;
    bool isOutput;
    GPIO_PinState lastState;
} RelayPin_t;

static bool s_shutdownHoldLow = false;

static RelayPin_t relayPins[RELAY_IO_COUNT] =
{
    { REL_L_GPIO_Port,             REL_L_Pin,             "REL_L",          false, GPIO_PIN_RESET },
    { REL_H_GPIO_Port,             REL_H_Pin,             "REL_H",          false, GPIO_PIN_RESET },
    { REL_I2C_ADD_GPIO_Port,       REL_I2C_ADD_Pin,       "REL_I2C_ADD",    true,  GPIO_PIN_RESET },
    { REL_EN_H_GPIO_Port,          REL_EN_H_Pin,          "REL_EN_H",       true,  GPIO_PIN_RESET },
    { REL_EN_L_GPIO_Port,          REL_EN_L_Pin,          "REL_EN_L",       true,  GPIO_PIN_RESET },
    { REL_SHUTDOWN_N_GPIO_Port,    REL_SHUTDOWN_N_Pin,    "REL_SHUTDOWN_N", true,  GPIO_PIN_RESET },
    { REL_RESET_N_GPIO_Port,       REL_RESET_N_Pin,       "REL_RESET_N",    true,  GPIO_PIN_RESET },
};

static GPIO_PinState readPin(RelayIoId_t id)
{
    return HAL_GPIO_ReadPin(relayPins[id].port, relayPins[id].pin);
}

void RelayIo_Init(void)
{
    for (uint32_t i = 0; i < RELAY_IO_COUNT; i++)
    {
        relayPins[i].lastState = readPin((RelayIoId_t)i);
    }
}

bool RelayIo_Update(void)
{
    bool changed = false;

    for (uint32_t i = 0; i < RELAY_IO_COUNT; i++)
    {
        GPIO_PinState state = readPin((RelayIoId_t)i);

        if (state != relayPins[i].lastState)
        {
            relayPins[i].lastState = state;
            changed = true;
        }
    }

    return changed;
}

void RelayIo_GetStates(RelayIoState_t *states, uint32_t maxCount)
{
    if (states == NULL)
    {
        return;
    }

    uint32_t count = (maxCount < RELAY_IO_COUNT) ? maxCount : RELAY_IO_COUNT;

    for (uint32_t i = 0; i < count; i++)
    {
        states[i].name = relayPins[i].name;
        states[i].isOutput = relayPins[i].isOutput;
        states[i].state = readPin((RelayIoId_t)i);
    }
}

GPIO_PinState RelayIo_ReadShutdownNLevel(void)
{
    return HAL_GPIO_ReadPin(REL_SHUTDOWN_N_GPIO_Port, REL_SHUTDOWN_N_Pin);
}

void RelayIo_ToggleResetN(void)
{
    HAL_GPIO_TogglePin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin);
}

void RelayIo_ToggleShutdownN(void)
{
    s_shutdownHoldLow = !s_shutdownHoldLow;

    if (s_shutdownHoldLow)
    {
        // Open-drain active: pull net low
        HAL_GPIO_WritePin(REL_SHUTDOWN_N_GPIO_Port,
                          REL_SHUTDOWN_N_Pin,
                          GPIO_PIN_RESET);
    }
    else
    {
        // Open-drain released: output transistor off
        HAL_GPIO_WritePin(REL_SHUTDOWN_N_GPIO_Port,
                          REL_SHUTDOWN_N_Pin,
                          GPIO_PIN_SET);
    }
}

bool RelayIo_GetShutdownNHoldLowCommand(void)
{
    return s_shutdownHoldLow;
}



void RelayIo_ToggleEnH(void)
{
    HAL_GPIO_TogglePin(REL_EN_H_GPIO_Port, REL_EN_H_Pin);
}

void RelayIo_ToggleEnL(void)
{
    HAL_GPIO_TogglePin(REL_EN_L_GPIO_Port, REL_EN_L_Pin);
}
