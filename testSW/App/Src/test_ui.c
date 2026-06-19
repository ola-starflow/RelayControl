#include "test_ui.h"
#include "debug_console.h"
#include "relay_io.h"
#include "slg47011.h"
#include "stm32g4xx_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *pinStateText(GPIO_PinState state)
{
    return (state == GPIO_PIN_SET) ? "HIGH" : "LOW";
}

static const char *bitText(uint8_t value, uint8_t bit)
{
    return ((value & (1u << bit)) != 0u) ? "HIGH" : "LOW";
}

/*
 * For an open-drain output:
 *   GPIO_PIN_RESET = actively pulling low
 *   GPIO_PIN_SET   = released / high-Z
 */
static const char *openDrainOutputText(GPIO_PinState commandedState)
{
    return (commandedState == GPIO_PIN_RESET) ? "HELD LOW" : "RELEASED";
}

static bool isShutdownPinName(const char *name)
{
    if (name == NULL)
    {
        return false;
    }

    return (strcmp(name, "REL_SHUTDOWN_N") == 0);
}

bool TestUi_HandleCommand(uint8_t ch)
{
    switch (ch)
    {
        case 'r':
        case 'R':
            RelayIo_ToggleResetN();
            return true;

        case 's':
        case 'S':
            RelayIo_ToggleShutdownN();
            return true;

        case 'h':
        case 'H':
            RelayIo_ToggleEnH();
            return true;

        case 'l':
        case 'L':
            RelayIo_ToggleEnL();
            return true;

        case 'd':
        case 'D':
            SLG47011_DumpDataBuffer2();
            DebugConsole_FlushMs(200u);
            return true;

        case 'g':
            printf("\r\nLoading generated SLG47011 config into volatile RAM...\r\n");
            if (SLG47011_LoadGeneratedRamConfig(false, NULL) == HAL_OK)
            {
                printf("RAM config load OK. This is volatile and will be lost on power cycle.\r\n");
            }
            else
            {
                printf("RAM config load FAILED, err=0x%08lX\r\n", SLG47011_GetLastError());
            }
            DebugConsole_FlushMs(200u);
            HAL_Delay(2000);
            return true;

        case 'G':
        {
            SLG47011_VerifyResult_t verify = {0};
            printf("\r\nLoading generated SLG47011 config into volatile RAM with verify...\r\n");
            if (SLG47011_LoadGeneratedRamConfig(true, &verify) == HAL_OK)
            {
                printf("RAM config load + verify OK. This is volatile and will be lost on power cycle.\r\n");
            }
            else if (!verify.ok)
            {
                printf("RAM config verify FAILED at 0x%04X: expected 0x%02X, read 0x%02X\r\n",
                       verify.failAddr, verify.expected, verify.actual);
            }
            else
            {
                printf("RAM config load FAILED, err=0x%08lX\r\n", SLG47011_GetLastError());
            }
            DebugConsole_FlushMs(200u);
            HAL_Delay(2000);
            return true;
        }

        case 'v':
        case 'V':
        {
            SLG47011_RamCompareResult_t cmp = {0};
            if (SLG47011_CompareGeneratedRamConfig(&cmp, true) == HAL_OK)
            {
                printf("RAM compare OK: %lu bytes match.\r\n",
                       (unsigned long)cmp.comparedBytes);
            }
            else if (cmp.mismatchCount > 0u)
            {
                printf("RAM compare FAILED: %lu mismatch(es). First at 0x%04X: expected 0x%02X, read 0x%02X\r\n",
                       (unsigned long)cmp.mismatchCount,
                       cmp.firstFailAddr,
                       cmp.firstExpected,
                       cmp.firstActual);
            }
            else
            {
                printf("RAM compare FAILED: I2C error=0x%08lX after %lu byte(s).\r\n",
                       SLG47011_GetLastError(),
                       (unsigned long)cmp.comparedBytes);
            }
            DebugConsole_FlushMs(200u);
            HAL_Delay(2000);
            return true;
        }

        default:
            return false;
    }
}

void TestUi_Print(const TestUiState_t *state)
{
    if (state == NULL)
    {
        return;
    }

    printf("\033[2J");
    printf("\033[H");

    printf("========================================\r\n");
    printf("        Relay test firmware status       \r\n");
    printf("========================================\r\n\r\n");

    printf("Commands:\r\n");
    printf("  r : toggle REL_RESET_N\r\n");
    printf("  s : toggle REL_SHUTDOWN_N\r\n");
    printf("  h : toggle REL_EN_H\r\n");
    printf("  l : toggle REL_EN_L\r\n");
    printf("  d : dump SLG47011 Data Buffer2\r\n");
    printf("  g : load generated GreenPAK config to volatile RAM\r\n");
    printf("  G : load generated GreenPAK config to RAM and verify\r\n");
    printf("  v : read back RAM and compare with generated config\r\n");
    printf("\r\n");

    printf("Inputs:\r\n");
    for (uint32_t i = 0; i < RELAY_IO_COUNT; i++)
    {
        if (!state->relay[i].isOutput)
        {
            printf("  %-20s : %s\r\n",
                   state->relay[i].name,
                   pinStateText(state->relay[i].state));
        }
    }

    printf("\r\nOutputs:\r\n");
    for (uint32_t i = 0; i < RELAY_IO_COUNT; i++)
    {
        if (state->relay[i].isOutput)
        {
            if (isShutdownPinName(state->relay[i].name))
            {
                bool holdLow = RelayIo_GetShutdownNHoldLowCommand();
                GPIO_PinState actualLevel = RelayIo_ReadShutdownNLevel();

                printf("  %-20s : OD=%s, level=%s\r\n",
                       state->relay[i].name,
                       holdLow ? "HELD LOW" : "RELEASED",
                       pinStateText(actualLevel));
            }
            else
            {
                printf("  %-20s : %s\r\n",
                       state->relay[i].name,
                       pinStateText(state->relay[i].state));
            }
        }
    }

    printf("\r\nSLG47011 Signal Readback:\r\n");
    if (state->signalReadback.ok)
    {
        printf("  Raw byte             : 0x%02X\r\n", state->signalReadback.raw);

        for (uint8_t bit = 0; bit < 8u; bit++)
        {
            printf("  Virtual Output %-5u : %s\r\n",
                   bit,
                   bitText(state->signalReadback.raw, bit));
        }
    }
    else
    {
        printf("  I2C read failed, err=0x%08lX\r\n", SLG47011_GetLastError());
    }

    printf("\r\nSLG47011 Temperature:\r\n");
    if (state->temperature.ok)
    {
        int32_t mv_x10 = (int32_t)(state->temperature.sensorMv * 10.0f);
        int32_t temp_x10 = (int32_t)(state->temperature.degC * 10.0f);

        printf("  Data Buffer2 raw    : 0x%04X / %u\r\n",
               state->temperature.raw,
               state->temperature.raw);

        printf("  TS voltage          : %ld.%01ld mV\r\n",
               mv_x10 / 10,
               labs(mv_x10 % 10));

        printf("  Temperature         : %ld.%01ld degC\r\n",
               temp_x10 / 10,
               labs(temp_x10 % 10));
    }
    else
    {
        printf("  Temperature read    : I2C read failed, err=0x%08lX\r\n",
               SLG47011_GetLastError());
    }
}