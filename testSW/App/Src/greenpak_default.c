#include "greenpak_default.h"
#include "greenpak_fixture.h"
#include "greenpak_host.h"
#include "manual_control.h"
#include "test_context.h"

#define GREENPAK_DEFAULT_SHUTDOWN_PULSE_MS        10u
#define GREENPAK_DEFAULT_WDT_RECOVERY_MS        2500u
#define GREENPAK_DEFAULT_RESET_LOW_US            100u
#define GREENPAK_DEFAULT_AFTER_RESET_HIGH_US      100u

bool GreenPakDefault_Apply(void)
{
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

    /*
     * Pulse shutdown before reset. If one or both relays are closed, this
     * forces the GreenPAK to run its normal safe shutdown sequence before the
     * reset/default-state recovery step.
     */
    ManualControl_SetShutdownReleased(false);
    TestContext_DelayMsWithService(GREENPAK_DEFAULT_SHUTDOWN_PULSE_MS);
    ManualControl_SetShutdownReleased(true);

    ManualControl_SetRelayEnableH(false);
    ManualControl_SetRelayEnableL(false);
    ManualControl_SetResetReleased(true);

    /*
     * Make WDT recovery deterministic.
     *
     * Even if the MCU-side auto-toggle flag says WDT was already enabled, the
     * GreenPAK may still be in a WDT/SDN-latched state from a previous test.
     * Always wait long enough for several OUT0 transitions to be observed by
     * the GreenPAK before trying to reset/clear the latched state.
     */
    TestContext_DelayMsWithService(GREENPAK_DEFAULT_WDT_RECOVERY_MS);

    ManualControl_SetResetReleased(false);
    TestContext_DelayUs(GREENPAK_DEFAULT_RESET_LOW_US);
    ManualControl_SetResetReleased(true);
    TestContext_DelayUs(GREENPAK_DEFAULT_AFTER_RESET_HIGH_US);

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
