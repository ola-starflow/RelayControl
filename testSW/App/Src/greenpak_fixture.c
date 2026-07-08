#include "greenpak_fixture.h"
#include "main.h"

#include <stdio.h>

#define DAC_ZERO_CODE 0u

static DAC_HandleTypeDef *s_hdacRelayPwrVoltage = NULL; /* PA4 / DAC1_OUT1 */
static DAC_HandleTypeDef *s_hdacShuntPlus = NULL;       /* PA6 / DAC2_OUT1 */
static bool s_dacRelayPwrVoltageStarted = false;
static bool s_dacShuntPlusStarted = false;

static const char *pinStateText(GPIO_PinState state)
{
    return (state == GPIO_PIN_SET) ? "HIGH" : "LOW";
}

void GreenPakFixture_Init(DAC_HandleTypeDef *hdacRelayPwrVoltage,
                          DAC_HandleTypeDef *hdacShuntPlus)
{
    s_hdacRelayPwrVoltage = hdacRelayPwrVoltage;
    s_hdacShuntPlus = hdacShuntPlus;
    s_dacRelayPwrVoltageStarted = false;
    s_dacShuntPlusStarted = false;
}

const char *GreenPakFixture_StatusText(GreenPakFixture_Status_t status)
{
    switch (status)
    {
        case GREENPAK_FIXTURE_OK: return "OK";
        case GREENPAK_FIXTURE_DAC1_START_FAILED: return "DAC1 start failed";
        case GREENPAK_FIXTURE_DAC2_START_FAILED: return "DAC2 start failed";
        case GREENPAK_FIXTURE_DAC1_SET_FAILED: return "DAC1 set failed";
        case GREENPAK_FIXTURE_DAC2_SET_FAILED: return "DAC2 set failed";
        default: return "Unknown fixture status";
    }
}

static GreenPakFixture_Status_t startDacIfNeeded(DAC_HandleTypeDef *hdac,
                                                 bool *started,
                                                 GreenPakFixture_Status_t failStatus)
{
    if ((hdac == NULL) || (started == NULL))
    {
        return failStatus;
    }

    if (!(*started))
    {
        if (HAL_DAC_Start(hdac, DAC_CHANNEL_1) != HAL_OK)
        {
            return failStatus;
        }

        *started = true;
    }

    return GREENPAK_FIXTURE_OK;
}

static void configureGpioOutput(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(port, pin, level);

    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void configureGpioInput(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin = pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(port, &GPIO_InitStruct);
}

void GreenPakFixture_ReleaseGreenPakOutputsToInputs(void)
{
    /* These are GreenPAK outputs after configuration. Keep the Nucleo high-Z. */
    configureGpioInput(PRE_CHARGE_EN_GPIO_Port, PRE_CHARGE_EN_Pin);
    configureGpioInput(REL_L_GPIO_Port, REL_L_Pin);
}


GreenPakFixture_Status_t GreenPakFixture_SetRelayPwrVoltageZero(void)
{
    GreenPakFixture_Status_t status = startDacIfNeeded(s_hdacRelayPwrVoltage,
                                                       &s_dacRelayPwrVoltageStarted,
                                                       GREENPAK_FIXTURE_DAC1_START_FAILED);
    if (status != GREENPAK_FIXTURE_OK)
    {
        return status;
    }

    if (HAL_DAC_SetValue(s_hdacRelayPwrVoltage, DAC_CHANNEL_1, DAC_ALIGN_12B_R, DAC_ZERO_CODE) != HAL_OK)
    {
        return GREENPAK_FIXTURE_DAC1_SET_FAILED;
    }

    return GREENPAK_FIXTURE_OK;
}

GreenPakFixture_Status_t GreenPakFixture_PrepareI2cScanAllAddressPinsLow(void)
{
    GreenPakFixture_Status_t status;

    /* Safe digital defaults before scanning. */
    HAL_GPIO_WritePin(REL_EN_H_GPIO_Port, REL_EN_H_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(REL_EN_L_GPIO_Port, REL_EN_L_Pin, GPIO_PIN_RESET);       /* SLA3 low */
    HAL_GPIO_WritePin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(REL_SHUTDOWN_N_GPIO_Port, REL_SHUTDOWN_N_Pin, GPIO_PIN_SET); /* open-drain release */

    status = startDacIfNeeded(s_hdacRelayPwrVoltage,
                              &s_dacRelayPwrVoltageStarted,
                              GREENPAK_FIXTURE_DAC1_START_FAILED);
    if (status != GREENPAK_FIXTURE_OK)
    {
        return status;
    }

    status = startDacIfNeeded(s_hdacShuntPlus,
                              &s_dacShuntPlusStarted,
                              GREENPAK_FIXTURE_DAC2_START_FAILED);
    if (status != GREENPAK_FIXTURE_OK)
    {
        return status;
    }

    /* Keep analog stimuli at 0 V for I2C search. PA6/shunt+ is also SLA2. */
    if (HAL_DAC_SetValue(s_hdacRelayPwrVoltage, DAC_CHANNEL_1, DAC_ALIGN_12B_R, DAC_ZERO_CODE) != HAL_OK)
    {
        return GREENPAK_FIXTURE_DAC1_SET_FAILED;
    }

    if (HAL_DAC_SetValue(s_hdacShuntPlus, DAC_CHANNEL_1, DAC_ALIGN_12B_R, DAC_ZERO_CODE) != HAL_OK)
    {
        return GREENPAK_FIXTURE_DAC2_SET_FAILED;
    }

    /* Force address-capable pins low during scan. */
    configureGpioOutput(PRE_CHARGE_EN_GPIO_Port, PRE_CHARGE_EN_Pin, GPIO_PIN_RESET); /* SLA0 */
    configureGpioOutput(REL_L_GPIO_Port, REL_L_Pin, GPIO_PIN_RESET);                 /* SLA1 */
    HAL_GPIO_WritePin(REL_EN_L_GPIO_Port, REL_EN_L_Pin, GPIO_PIN_RESET);             /* SLA3 */

    return GREENPAK_FIXTURE_OK;
}

void GreenPakFixture_PrintScanFixtureStatus(void)
{
    printf("\r\nGreenPAK I2C scan fixture state:\r\n");
    printf("  Address pins are forced low before scan:\r\n");
    printf("    SLA0 / PB10 / TP5  PreChargeEN : %s (temporary output)\r\n",
           pinStateText(HAL_GPIO_ReadPin(PRE_CHARGE_EN_GPIO_Port, PRE_CHARGE_EN_Pin)));
    printf("    SLA1 / PB1  / TP14 Relay L     : %s (temporary output)\r\n",
           pinStateText(HAL_GPIO_ReadPin(REL_L_GPIO_Port, REL_L_Pin)));
    printf("    SLA2 / PA6  / TP7  shunt+      : 0 V from DAC2\r\n");
    printf("    SLA3 / PB14 / TP15 Enable L    : %s\r\n",
           pinStateText(HAL_GPIO_ReadPin(REL_EN_L_GPIO_Port, REL_EN_L_Pin)));
    printf("  Other control pins:\r\n");
    printf("    Enable H    PB13 -> TP9  : %s\r\n",
           pinStateText(HAL_GPIO_ReadPin(REL_EN_H_GPIO_Port, REL_EN_H_Pin)));
    printf("    Reset_N     PA9  -> TP12 : %s\r\n",
           pinStateText(HAL_GPIO_ReadPin(REL_RESET_N_GPIO_Port, REL_RESET_N_Pin)));
    printf("    Shutdown_N  PA8  -> TP6  : %s (open-drain, HIGH = released)\r\n",
           pinStateText(HAL_GPIO_ReadPin(REL_SHUTDOWN_N_GPIO_Port, REL_SHUTDOWN_N_Pin)));
    printf("    DAC1 PA4 -> TP13 Relay pwr voltage : 0 V\r\n");
}
