#include "appMain.h"
#include "main.h"

#include "debug_console.h"
#include "relay_io.h"
#include "slg47011.h"
#include "test_ui.h"

#include <stdbool.h>
#include <stdint.h>

extern I2C_HandleTypeDef hi2c1;

#define SLG47011_POLL_INTERVAL_MS  200u
#define TERMINAL_FLUSH_MS          200u

static TestUiState_t uiState;
static uint32_t lastSlgPollTime = 0;

static void updateRelayUiState(void)
{
    RelayIo_GetStates(uiState.relay, RELAY_IO_COUNT);
}

static void updateSlgUiState(void)
{
    SLG47011_ReadSignalReadback(&uiState.signalReadback);
    SLG47011_ReadTemperature(&uiState.temperature);
    SLG47011_ReadHostOutputs(&uiState.hostOutputs);
}

static void printUi(void)
{
    updateRelayUiState();
    updateSlgUiState();
    TestUi_Print(&uiState);
    DebugConsole_FlushMs(TERMINAL_FLUSH_MS);
}

void App_Init(void)
{
    DebugConsole_Init();
    RelayIo_Init();
    SLG47011_Init(&hi2c1);

    updateRelayUiState();
    updateSlgUiState();
    printUi();

    SLG47011_PrintI2cScan();
    DebugConsole_FlushMs(TERMINAL_FLUSH_MS);
}

void App_Run(void)
{
    uint8_t rxData = 0;

    while (1)
    {
        bool redraw = false;

        if (DebugConsole_ReadChar(&rxData))
        {
            if (TestUi_HandleCommand(rxData))
            {
                redraw = true;
            }
        }

        if (RelayIo_Update())
        {
            redraw = true;
        }

        uint32_t now = HAL_GetTick();

        SLG47011_ServiceWdtKick(now);

        if ((now - lastSlgPollTime) >= SLG47011_POLL_INTERVAL_MS)
        {
            lastSlgPollTime = now;

            /*
             * Keep SLG telemetry fresh internally, but do not redraw the
             * serial terminal just because temperature/readback data changes.
             * Commands and physical GPIO changes still trigger redraws.
             */
            updateSlgUiState();
        }

        if (redraw)
        {
            printUi();
        }

        DebugConsole_Update();
    }
}
