#include "manual_control.h"
#include "greenpak_fixture.h"
#include "main.h"

#include <stdbool.h>
#include <stdio.h>

static bool s_shutdownDrivenLow = false;

static const char *pinStateText(GPIO_PinState state)
{
    return (state == GPIO_PIN_SET) ? "HIGH" : "LOW";
}

static const char *boolText(bool value)
{
    return value ? "YES" : "NO";
}

static bool pinIsLow(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET);
}

static void setShutdownDrivenLow(bool driveLow)
{
    s_shutdownDrivenLow = driveLow;

    /* PA8 is open-drain. LOW actively pulls Shutdown_N low. HIGH releases it. */
    HAL_GPIO_WritePin(REL_SHUTDOWN_N_GPIO_Port,
                      REL_SHUTDOWN_N_Pin,
                      driveLow ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void ManualControl_Init(void)
{
    /* Make sure GreenPAK output pins are inputs before manual status/control mode. */
    GreenPakFixture_ReleaseGreenPakOutputsToInputs();

    HAL_GPIO_WritePin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(REL_EN_H_GPIO_Port, REL_EN_H_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(REL_EN_L_GPIO_Port, REL_EN_L_Pin, GPIO_PIN_RESET);
    GreenPakFixture_SetCurrentDirectionLow();
    setShutdownDrivenLow(false);
}

void ManualControl_SetResetReleased(bool released)
{
    HAL_GPIO_WritePin(REL_RESET_N_GPIO_Port,
                      REL_RESET_N_Pin,
                      released ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ManualControl_SetShutdownReleased(bool released)
{
    setShutdownDrivenLow(!released);
}

void ManualControl_SetRelayEnableH(bool high)
{
    HAL_GPIO_WritePin(REL_EN_H_GPIO_Port,
                      REL_EN_H_Pin,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ManualControl_SetRelayEnableL(bool high)
{
    HAL_GPIO_WritePin(REL_EN_L_GPIO_Port,
                      REL_EN_L_Pin,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void ManualControl_ToggleReset(void)
{
    GPIO_PinState next = (HAL_GPIO_ReadPin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin) == GPIO_PIN_SET) ?
                         GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin, next);
}

void ManualControl_ToggleShutdown(void)
{
    setShutdownDrivenLow(!s_shutdownDrivenLow);
}

void ManualControl_ToggleRelayEnableH(void)
{
    GPIO_PinState next = (HAL_GPIO_ReadPin(REL_EN_H_GPIO_Port, REL_EN_H_Pin) == GPIO_PIN_SET) ?
                         GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(REL_EN_H_GPIO_Port, REL_EN_H_Pin, next);
}

void ManualControl_ToggleRelayEnableL(void)
{
    GPIO_PinState next = (HAL_GPIO_ReadPin(REL_EN_L_GPIO_Port, REL_EN_L_Pin) == GPIO_PIN_SET) ?
                         GPIO_PIN_RESET : GPIO_PIN_SET;
    HAL_GPIO_WritePin(REL_EN_L_GPIO_Port, REL_EN_L_Pin, next);
}

void ManualControl_SetCurrentDirection(bool high)
{
    if (high)
    {
        GreenPakFixture_SetCurrentDirectionHigh();
    }
    else
    {
        GreenPakFixture_SetCurrentDirectionLow();
    }
}

void ManualControl_ToggleCurrentDirection(void)
{
    ManualControl_SetCurrentDirection(!GreenPakFixture_GetCurrentDirectionHigh());
}

void ManualControl_GetStatus(ManualControl_Status_t *status)
{
    if (status == NULL)
    {
        return;
    }

    status->resetN = HAL_GPIO_ReadPin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin);
    status->shutdownNPin = HAL_GPIO_ReadPin(REL_SHUTDOWN_N_GPIO_Port, REL_SHUTDOWN_N_Pin);
    status->shutdownMcuDrivesLow = s_shutdownDrivenLow;
    status->shutdownPulledLowExternally = (!s_shutdownDrivenLow) &&
                                          (status->shutdownNPin == GPIO_PIN_RESET);
    status->relEnH = HAL_GPIO_ReadPin(REL_EN_H_GPIO_Port, REL_EN_H_Pin);
    status->relEnL = HAL_GPIO_ReadPin(REL_EN_L_GPIO_Port, REL_EN_L_Pin);
    status->currentDirection = GreenPakFixture_GetCurrentDirectionHigh() ? GPIO_PIN_SET : GPIO_PIN_RESET;

    status->preChargeEn = HAL_GPIO_ReadPin(PRE_CHARGE_EN_GPIO_Port, PRE_CHARGE_EN_Pin);
    status->relayL = HAL_GPIO_ReadPin(REL_L_GPIO_Port, REL_L_Pin);
    status->relayH = HAL_GPIO_ReadPin(REL_H_GPIO_Port, REL_H_Pin);
    status->boostPower = HAL_GPIO_ReadPin(REL_BOOST_PWR_GPIO_Port, REL_BOOST_PWR_Pin);
    status->relayPwrEn = HAL_GPIO_ReadPin(REL_PWR_EN_GPIO_Port, REL_PWR_EN_Pin);
}

void ManualControl_PrintStatus(void)
{
    ManualControl_Status_t status;
    ManualControl_GetStatus(&status);

    printf("\r\nManual control status:\r\n");
    printf("  Nucleo -> GreenPAK controls:\r\n");
    printf("    Reset_N      PA9  -> TP12 : %s\r\n", pinStateText(status.resetN));
    printf("    Shutdown_N   PA8  -> TP6  : MCU drive low = %s, pin is low = %s, pin level = %s\r\n",
           boolText(status.shutdownMcuDrivesLow),
           boolText(status.shutdownNPin == GPIO_PIN_RESET),
           pinStateText(status.shutdownNPin));
    if (status.shutdownPulledLowExternally)
    {
        printf("      Note: Shutdown_N is released by MCU but still low, so GreenPAK/external circuitry is pulling it low.\r\n");
    }
    printf("    REL_EN_H     PB13 -> TP9  : %s\r\n", pinStateText(status.relEnH));
    printf("    REL_EN_L     PB14 -> TP15 : %s\r\n", pinStateText(status.relEnL));
    printf("    CURR_DIR     PA6  -> TP7  : %s\r\n", pinStateText(status.currentDirection));

    printf("  GreenPAK -> Nucleo outputs/readbacks:\r\n");
    printf("    PreChargeEN  PB10 <- TP5  : %s\r\n", pinStateText(status.preChargeEn));
    printf("    Relay L      PB1  <- TP14 : %s\r\n", pinStateText(status.relayL));
    printf("    Relay H      PB15 <- TP8  : %s\r\n", pinStateText(status.relayH));
    printf("    Boost power  PC7  <- TP10 : %s\r\n", pinStateText(status.boostPower));
    printf("    Relay PWR EN PA10 <- TP16 : %s\r\n", pinStateText(status.relayPwrEn));
}
