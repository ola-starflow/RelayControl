#ifndef TEST_UI_H
#define TEST_UI_H

#include "relay_io.h"
#include "slg47011.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    RelayIoState_t relay[RELAY_IO_COUNT];
    SLG47011_SignalReadback_t signalReadback;
    SLG47011_Temperature_t temperature;
    SLG47011_HostOutputs_t hostOutputs;
} TestUiState_t;

void TestUi_Print(const TestUiState_t *state);
bool TestUi_HandleCommand(uint8_t ch);

#endif /* TEST_UI_H */
