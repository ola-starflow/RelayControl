#include "greenpak_default.h"
#include "greenpak_fixture.h"
#include "greenpak_host.h"
#include "manual_control.h"
#include "test_context.h"

bool GreenPakDefault_Apply(void)
{
    bool wdtWasEnabled = GreenPakHost_GetWdtAutoToggleEnabled();
    bool ok = true;

    GreenPakFixture_ReleaseGreenPakOutputsToInputs();

    if (GreenPakHost_SetWdtAutoToggle(true) != HAL_OK)
    {
        ok = false;
    }

    if (GreenPakHost_SetRelayPowerEnable(false) != HAL_OK)
    {
        ok = false;
    }

    if (GreenPakFixture_SetRelayPwrVoltageZero() != GREENPAK_FIXTURE_OK)
    {
        ok = false;
    }
    ManualControl_SetShutdownReleased(false);
    TestContext_DelayMsWithService(10u);
    ManualControl_SetShutdownReleased(true);
    ManualControl_SetRelayEnableH(false);
    ManualControl_SetRelayEnableL(false);
    ManualControl_SetResetReleased(true);

    TestContext_DelayMsWithService(60u);

    if (!wdtWasEnabled)
    {
        TestContext_DelayMsWithService(1050u);
    }

    ManualControl_SetResetReleased(false);
    TestContext_DelayUs(100u);
    ManualControl_SetResetReleased(true);
    TestContext_DelayUs(100u);

    return ok && GreenPakDefault_VerifyOutputs();
}

bool GreenPakDefault_VerifyOutputs(void)
{
    bool ok = true;

    ok &= TestContext_CheckPin(TEST_PIN_SHUTDOWN_N, GPIO_PIN_SET);
    ok &= TestContext_CheckPin(TEST_PIN_REL_H, GPIO_PIN_RESET);
    ok &= TestContext_CheckPin(TEST_PIN_REL_L, GPIO_PIN_RESET);
    ok &= TestContext_CheckPin(TEST_PIN_PRE_CHARGE, GPIO_PIN_RESET);
    ok &= TestContext_CheckPin(TEST_PIN_BOOST_PWR, GPIO_PIN_RESET);
    ok &= TestContext_CheckPin(TEST_PIN_REL_PWR_EN, GPIO_PIN_RESET);

    return ok;
}
