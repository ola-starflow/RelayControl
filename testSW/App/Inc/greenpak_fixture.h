#ifndef GREENPAK_FIXTURE_H
#define GREENPAK_FIXTURE_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32g4xx_hal.h"

/*
 * Test fixture signal mapping
 *
 * Nucleo G474RE -> SLG47011V-DIP
 *   PB8  -> TP3  SCL
 *   PB9  -> TP4  SDA
 *   PB13 -> TP9  Enable H
 *   PB14 -> TP15 Enable L / SLA3
 *   PA8  -> TP6  Shutdown_N, open drain, external pull-up
 *   PA9  -> TP12 Reset_N
 *   PA4  -> TP13 Relay pwr voltage, DAC1_OUT1
 *   PA6  -> TP7  shunt+, DAC2_OUT1 / SLA2
 *   PB10 -> TP5  PreChargeEN / SLA0
 *   PB1  -> TP14 Relay L / SLA1
 *   PB15 <- TP8  Relay H
 *   PC7  <- TP10 Boost power
 *   PA10 <- TP16 Relay PWR EN
 *
 * The configured GreenPAK uses fixed I2C address 0x08.
 * Before the I2C scan, the fixture still forces all address-capable pins low:
 *   SLA0/PB10 = 0, SLA1/PB1 = 0, SLA2/PA6 DAC = 0 V, SLA3/PB14 = 0.
 */

typedef enum
{
    GREENPAK_FIXTURE_OK = 0,
    GREENPAK_FIXTURE_DAC1_START_FAILED,
    GREENPAK_FIXTURE_DAC2_START_FAILED,
    GREENPAK_FIXTURE_DAC1_SET_FAILED,
    GREENPAK_FIXTURE_DAC2_SET_FAILED
} GreenPakFixture_Status_t;

void GreenPakFixture_Init(DAC_HandleTypeDef *hdacRelayPwrVoltage,
                          DAC_HandleTypeDef *hdacShuntPlus);

GreenPakFixture_Status_t GreenPakFixture_PrepareI2cScanAllAddressPinsLow(void);
void GreenPakFixture_ReleaseGreenPakOutputsToInputs(void);
void GreenPakFixture_PrintScanFixtureStatus(void);
const char *GreenPakFixture_StatusText(GreenPakFixture_Status_t status);

#endif /* GREENPAK_FIXTURE_H */
