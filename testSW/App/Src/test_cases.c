#include "test_cases.h"
#include "greenpak_default.h"
#include "greenpak_fixture.h"
#include "manual_control.h"
#include "test_context.h"
#include "greenpak_host.h"

#include <stddef.h>
#include <stdio.h>

#define TEST_MAX_CHECKPOINTS 16u
#define ANALOG_MAX_CHECKPOINTS 24u
#define ADC_RAW_MAX 16383u
#define ADC_VREF_MV 1620u
#define RELAY_DIVIDER_NUMERATOR 11u
#define TEMP_SENSOR_25C_UV 753800u
#define TEMP_SENSOR_SLOPE_UV_C 1830u
#define ANALOG_RELAY_VOLTAGE_TOLERANCE_MV 500u
#define ANALOG_ADC_POWER_UP_MS 100u
#define ANALOG_MOVING_AVG_SETTLE_MS 1200u
#define ANALOG_ADC_OFF_HOLD_MS 1200u
#define ANALOG_BOOST_HIGH_TIMEOUT_MS 20u
#define ANALOG_BOOST_LOW_TIMEOUT_MS 200u
#define ANALOG_DB1_AFTER_BOOST_HOLD_MS 1200u

typedef struct
{
    GPIO_PinState shutdownN;
    GPIO_PinState relL;
    GPIO_PinState relH;
    GPIO_PinState preCharge;
    GPIO_PinState boostPwr;
    GPIO_PinState relPwrEn;
} PinSnapshot_t;

typedef struct
{
    bool check;
    GPIO_PinState value;
} ExpectedPin_t;

typedef struct
{
    ExpectedPin_t shutdownN;
    ExpectedPin_t relL;
    ExpectedPin_t relH;
    ExpectedPin_t preCharge;
    ExpectedPin_t boostPwr;
    ExpectedPin_t relPwrEn;
} ExpectedSnapshot_t;

typedef struct
{
    const char *timeText;
    const char *label;
    ExpectedSnapshot_t expected;
    PinSnapshot_t actual;
    bool pass;
} TestCheckpoint_t;

typedef struct
{
    TestCheckpoint_t items[TEST_MAX_CHECKPOINTS];
    uint32_t count;
    bool overflow;
} TestReport_t;

static const char *levelText(GPIO_PinState state)
{
    return (state == GPIO_PIN_SET) ? "HIGH" : "LOW";
}

static ExpectedPin_t expectPin(GPIO_PinState value)
{
    ExpectedPin_t pin;
    pin.check = true;
    pin.value = value;
    return pin;
}

static ExpectedPin_t ignorePin(void)
{
    ExpectedPin_t pin;
    pin.check = false;
    pin.value = GPIO_PIN_RESET;
    return pin;
}

static ExpectedSnapshot_t makeExpectedFull(GPIO_PinState relL,
                                           GPIO_PinState relH,
                                           GPIO_PinState preCharge,
                                           GPIO_PinState boostPwr)
{
    ExpectedSnapshot_t state;

    state.shutdownN = expectPin(GPIO_PIN_SET);
    state.relL = expectPin(relL);
    state.relH = expectPin(relH);
    state.preCharge = expectPin(preCharge);
    state.boostPwr = expectPin(boostPwr);
    state.relPwrEn = expectPin(GPIO_PIN_RESET);

    return state;
}

static ExpectedSnapshot_t makeExpectedPreChargeTest(GPIO_PinState relL,
                                                    GPIO_PinState relH,
                                                    GPIO_PinState preCharge)
{
    ExpectedSnapshot_t state = makeExpectedFull(relL, relH, preCharge, GPIO_PIN_RESET);

    /* BOOST_PWR is verified by Test 2, not by the PreChargeEn behavior test. */
    state.boostPwr = ignorePin();

    return state;
}

static ExpectedSnapshot_t makeExpectedBoostTest(GPIO_PinState relL,
                                                GPIO_PinState relH,
                                                GPIO_PinState boostPwr)
{
    ExpectedSnapshot_t state = makeExpectedFull(relL, relH, GPIO_PIN_RESET, boostPwr);

    /* PreChargeEn is verified by Test 1, not by the Boost power behavior test. */
    state.preCharge = ignorePin();

    return state;
}

static PinSnapshot_t readSnapshot(void)
{
    PinSnapshot_t state;

    state.shutdownN = TestContext_ReadPin(TEST_PIN_SHUTDOWN_N);
    state.relL = TestContext_ReadPin(TEST_PIN_REL_L);
    state.relH = TestContext_ReadPin(TEST_PIN_REL_H);
    state.preCharge = TestContext_ReadPin(TEST_PIN_PRE_CHARGE);
    state.boostPwr = TestContext_ReadPin(TEST_PIN_BOOST_PWR);
    state.relPwrEn = TestContext_ReadPin(TEST_PIN_REL_PWR_EN);

    return state;
}

static bool expectedPinMatches(GPIO_PinState actual, ExpectedPin_t expected)
{
    if (!expected.check)
    {
        return true;
    }

    return (actual == expected.value);
}

static bool snapshotMatches(const PinSnapshot_t *actual, const ExpectedSnapshot_t *expected)
{
    return (expectedPinMatches(actual->shutdownN, expected->shutdownN) &&
            expectedPinMatches(actual->relL, expected->relL) &&
            expectedPinMatches(actual->relH, expected->relH) &&
            expectedPinMatches(actual->preCharge, expected->preCharge) &&
            expectedPinMatches(actual->boostPwr, expected->boostPwr) &&
            expectedPinMatches(actual->relPwrEn, expected->relPwrEn));
}

static void addCheckpoint(TestReport_t *report,
                          const char *timeText,
                          const char *label,
                          ExpectedSnapshot_t expected,
                          bool prerequisiteOk)
{
    if (report == NULL)
    {
        return;
    }

    if (report->count >= TEST_MAX_CHECKPOINTS)
    {
        report->overflow = true;
        return;
    }

    TestCheckpoint_t *item = &report->items[report->count++];

    item->timeText = timeText;
    item->label = label;
    item->expected = expected;
    item->actual = readSnapshot();
    item->pass = prerequisiteOk && snapshotMatches(&item->actual, &item->expected);
}

static void printExpectedPin(ExpectedPin_t pin)
{
    if (pin.check)
    {
        printf("%-7s", levelText(pin.value));
    }
    else
    {
        printf("%-7s", "IGNORED");
    }
}

static void printActualSnapshotLine(const char *prefix, const PinSnapshot_t *state)
{
    printf("    %s SHDN=%-4s REL_L=%-4s REL_H=%-4s PRE=%-4s BOOST=%-4s PWR_EN=%-4s\r\n",
           prefix,
           levelText(state->shutdownN),
           levelText(state->relL),
           levelText(state->relH),
           levelText(state->preCharge),
           levelText(state->boostPwr),
           levelText(state->relPwrEn));
}

static void printExpectedSnapshotLine(const char *prefix, const ExpectedSnapshot_t *state)
{
    printf("    %s SHDN=", prefix);
    printExpectedPin(state->shutdownN);
    printf("REL_L=");
    printExpectedPin(state->relL);
    printf("REL_H=");
    printExpectedPin(state->relH);
    printf("PRE=");
    printExpectedPin(state->preCharge);
    printf("BOOST=");
    printExpectedPin(state->boostPwr);
    printf("PWR_EN=");
    printExpectedPin(state->relPwrEn);
    printf("\r\n");
}

static void printReportHeader(const char *testNumber, const char *name, const char *intention)
{
    printf("\r\nTest %s: %s\r\n", testNumber, name);
    printf("Intention: %s\r\n", intention);
}

static void printReport(const TestReport_t *report, bool onlyFailures)
{
    printf("\r\nCheckpoints:\r\n");
    for (uint32_t i = 0u; i < report->count; i++)
    {
        const TestCheckpoint_t *item = &report->items[i];

        if (onlyFailures && item->pass)
        {
            continue;
        }

        printf("  %-8s  %-48s %s\r\n",
               item->timeText,
               item->label,
               item->pass ? "PASS" : "FAIL");

        if (!item->pass)
        {
            printExpectedSnapshotLine("expected", &item->expected);
            printActualSnapshotLine("actual  ", &item->actual);
        }
    }

    if (report->overflow)
    {
        printf("  WARNING: report checkpoint buffer overflowed. Increase TEST_MAX_CHECKPOINTS.\r\n");
    }
}

static bool reportPassed(const TestReport_t *report)
{
    bool ok = !report->overflow;

    for (uint32_t i = 0u; i < report->count; i++)
    {
        ok &= report->items[i].pass;
    }

    return ok;
}

static void printTestSummary(const char *testNumber, bool ok)
{
    printf("Test %s %s\r\n", testNumber, ok ? "passed" : "failed");
}

static void printCompletedSubtestSummary(const char *testNumber, const char *name, bool ok)
{
    printf("Test %s %s: %s\r\n", testNumber, ok ? "passed" : "failed", name);
}

static TestResult_t Test_VerifyPreChargeEnNormalBehavior(TestRunMode_t mode)
{
    TestReport_t report = {0};
    bool defaultOk = GreenPakDefault_Apply();

    addCheckpoint(&report,
                  "0 ms",
                  "Default state",
                  makeExpectedPreChargeTest(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                  defaultOk);

    ManualControl_SetRelayEnableL(true);
    uint32_t t0 = HAL_GetTick();

    TestContext_WaitUntilMs(t0 + 30u);
    addCheckpoint(&report,
                  "30 ms",
                  "After REL_EN_L high",
                  makeExpectedPreChargeTest(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                  true);

    TestContext_WaitUntilMs(t0 + 60u);
    addCheckpoint(&report,
                  "60 ms",
                  "After REL_EN_L high",
                  makeExpectedPreChargeTest(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    TestContext_WaitUntilMs(t0 + 1560u);
    addCheckpoint(&report,
                  "1560 ms",
                  "After REL_EN_L high",
                  makeExpectedPreChargeTest(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    ManualControl_SetRelayEnableH(true);
    uint32_t t1 = HAL_GetTick();

    TestContext_WaitUntilMs(t1 + 30u);
    addCheckpoint(&report,
                  "30 ms",
                  "After REL_EN_H high",
                  makeExpectedPreChargeTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_SET),
                  true);

    TestContext_WaitUntilMs(t1 + 60u);
    addCheckpoint(&report,
                  "60 ms",
                  "After REL_EN_H high",
                  makeExpectedPreChargeTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_RESET),
                  true);

    TestContext_WaitUntilMs(t1 + 1560u);
    addCheckpoint(&report,
                  "1560 ms",
                  "After REL_EN_H high",
                  makeExpectedPreChargeTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_RESET),
                  true);

    bool ok = reportPassed(&report);

    if ((mode == TEST_RUN_MODE_DETAILED) || !ok)
    {
        printReportHeader("1",
                          "Verify PreChargeEn normal behavior",
                          "Verify PreChargeEn works in a normal relay close sequence.");
        printf("Note: BOOST_PWR is ignored in this test and will be covered by Test 2.\r\n");
        printReport(&report, mode == TEST_RUN_MODE_RUN_ALL);
        printf("\r\nTest 1 result: %s\r\n", ok ? "PASS" : "FAIL");
    }
    else
    {
        printTestSummary("1", ok);
    }

    return ok ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

static void runBoostSimpleSequence(TestReport_t *report,
                                   const char *name,
                                   bool enableL,
                                   bool enableH)
{
    bool defaultOk = GreenPakDefault_Apply();

    addCheckpoint(report,
                  "0 ms",
                  "Default state",
                  makeExpectedFull(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                  defaultOk);

    ManualControl_SetRelayEnableL(enableL);
    ManualControl_SetRelayEnableH(enableH);

    TestContext_DelayUs(100u);
    addCheckpoint(report,
                  "100 us",
                  "100 us after enable",
                  makeExpectedBoostTest(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(20u);
    addCheckpoint(report,
                  "+20 ms",
                  "+20 ms after enable",
                  makeExpectedBoostTest(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Relay output asserted",
                  makeExpectedBoostTest(enableL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        enableH ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Relay output stable",
                  makeExpectedBoostTest(enableL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        enableH ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(30u);
    addCheckpoint(report,
                  "+30 ms",
                  "Extended boost on",
                  makeExpectedBoostTest(enableL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        enableH ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Boost off",
                  makeExpectedBoostTest(enableL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        enableH ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        GPIO_PIN_RESET),
                  true);

    (void)name;
}

static void runBoostLThenHAfter40MsSequence(TestReport_t *report)
{
    bool defaultOk = GreenPakDefault_Apply();

    addCheckpoint(report,
                  "0 ms",
                  "Default state",
                  makeExpectedFull(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                  defaultOk);

    ManualControl_SetRelayEnableL(true);

    TestContext_DelayUs(100u);
    addCheckpoint(report,
                  "100 us",
                  "100 us after REL_EN_L",
                  makeExpectedBoostTest(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(40u);
    addCheckpoint(report,
                  "+40 ms",
                  "L relay closed, boost still on",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    ManualControl_SetRelayEnableH(true);

    TestContext_DelayUs(100u);
    addCheckpoint(report,
                  "100 us",
                  "100 us after REL_EN_H",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(20u);
    addCheckpoint(report,
                  "+20 ms",
                  "+20 ms after REL_EN_H",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_RESET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Both relays closed",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(40u);
    addCheckpoint(report,
                  "+40 ms",
                  "Both relays stable, boost still on",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Boost off",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_RESET),
                  true);
}

static void runBoostStaggeredAfter80MsSequence(TestReport_t *report,
                                               bool firstIsL)
{
    bool defaultOk = GreenPakDefault_Apply();

    addCheckpoint(report,
                  "0 ms",
                  "Default state",
                  makeExpectedFull(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                  defaultOk);

    if (firstIsL)
    {
        ManualControl_SetRelayEnableL(true);
    }
    else
    {
        ManualControl_SetRelayEnableH(true);
    }

    TestContext_DelayMsWithService(80u);
    addCheckpoint(report,
                  "+80 ms",
                  "First relay stable, boost off",
                  makeExpectedBoostTest(firstIsL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        firstIsL ? GPIO_PIN_RESET : GPIO_PIN_SET,
                                        GPIO_PIN_RESET),
                  true);

    if (firstIsL)
    {
        ManualControl_SetRelayEnableH(true);
    }
    else
    {
        ManualControl_SetRelayEnableL(true);
    }

    TestContext_DelayUs(100u);
    addCheckpoint(report,
                  "100 us",
                  "100 us after second enable",
                  makeExpectedBoostTest(firstIsL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        firstIsL ? GPIO_PIN_RESET : GPIO_PIN_SET,
                                        GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(20u);
    addCheckpoint(report,
                  "+20 ms",
                  "+20 ms after second enable",
                  makeExpectedBoostTest(firstIsL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                        firstIsL ? GPIO_PIN_RESET : GPIO_PIN_SET,
                                        GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Both relays closed",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Both relays stable",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(30u);
    addCheckpoint(report,
                  "+30 ms",
                  "Extended boost on",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_SET),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Boost off",
                  makeExpectedBoostTest(GPIO_PIN_SET, GPIO_PIN_SET, GPIO_PIN_RESET),
                  true);
}

typedef void (*BoostSubtestFunction_t)(TestReport_t *report);

typedef struct
{
    const char *number;
    const char *name;
    BoostSubtestFunction_t run;
} BoostSubtest_t;

static void runBoostSubtest_LOnly(TestReport_t *report) { runBoostSimpleSequence(report, "L only", true, false); }
static void runBoostSubtest_HOnly(TestReport_t *report) { runBoostSimpleSequence(report, "H only", false, true); }
static void runBoostSubtest_LAndHTogether(TestReport_t *report) { runBoostSimpleSequence(report, "L+H together", true, true); }
static void runBoostSubtest_LThenHAfter40Ms(TestReport_t *report) { runBoostLThenHAfter40MsSequence(report); }
static void runBoostSubtest_LThenHAfter80Ms(TestReport_t *report) { runBoostStaggeredAfter80MsSequence(report, true); }
static void runBoostSubtest_HThenLAfter80Ms(TestReport_t *report) { runBoostStaggeredAfter80MsSequence(report, false); }

static const BoostSubtest_t s_boostSubtests[] =
{
    { "2.1", "L only close sequence", runBoostSubtest_LOnly },
    { "2.2", "H only close sequence", runBoostSubtest_HOnly },
    { "2.3", "L+H together close sequence", runBoostSubtest_LAndHTogether },
    { "2.4", "L then H, H enabled while L boost window is active", runBoostSubtest_LThenHAfter40Ms },
    { "2.5", "L stable first, then H", runBoostSubtest_LThenHAfter80Ms },
    { "2.6", "H stable first, then L", runBoostSubtest_HThenLAfter80Ms },
};

static TestResult_t Test_VerifyBoostPowerNormalBehavior(TestRunMode_t mode)
{
    bool allOk = true;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        printReportHeader("2",
                          "Verify Boost power normal behavior",
                          "Verify Boost power asserts during normal relay close sequences and releases afterwards.");
        printf("Note: PRE_CHARGE is ignored in this test and is covered by Test 1.\r\n");
    }

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_boostSubtests) / sizeof(s_boostSubtests[0])); i++)
    {
        TestReport_t report = {0};
        const BoostSubtest_t *subtest = &s_boostSubtests[i];

        subtest->run(&report);
        bool ok = reportPassed(&report);
        allOk &= ok;

        if ((mode == TEST_RUN_MODE_DETAILED) || !ok)
        {
            if (mode == TEST_RUN_MODE_RUN_ALL)
            {
                printReportHeader(subtest->number,
                                  subtest->name,
                                  "Verify Boost power behavior for this close sequence.");
                printf("Note: PRE_CHARGE is ignored in this test and is covered by Test 1.\r\n");
            }
            else
            {
                printf("\r\nSub-test %s: %s\r\n", subtest->number, subtest->name);
            }
            printReport(&report, mode == TEST_RUN_MODE_RUN_ALL);
            printf("Test %s result: %s\r\n", subtest->number, ok ? "PASS" : "FAIL");
        }
        else
        {
            printCompletedSubtestSummary(subtest->number, subtest->name, ok);
        }
    }

    bool finalDefaultOk = GreenPakDefault_Apply();
    if (mode == TEST_RUN_MODE_DETAILED)
    {
        TestReport_t report = {0};
        addCheckpoint(&report,
                      "0 ms",
                      "Final default state",
                      makeExpectedFull(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                      finalDefaultOk);
        printf("\r\nFinal default-state check\r\n");
        printReport(&report, false);
        printf("Final default-state result: %s\r\n", reportPassed(&report) ? "PASS" : "FAIL");
    }
    allOk &= finalDefaultOk;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        printf("\r\nTest 2 result: %s\r\n", allOk ? "PASS" : "FAIL");
    }
    else if (!allOk)
    {
        printf("Test 2 failed\r\n");
    }

    return allOk ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}


typedef enum
{
    SHUTDOWN_CURRENT_LOW = 0,
    SHUTDOWN_CURRENT_HIGH
} ShutdownCurrentDirection_t;

typedef enum
{
    SHUTDOWN_PERTURB_NONE = 0,
    SHUTDOWN_PERTURB_AFTER_START
} ShutdownPerturbation_t;


static ExpectedSnapshot_t makeExpectedRelayClosedForShutdownTest(GPIO_PinState relL,
                                                                GPIO_PinState relH)
{
    ExpectedSnapshot_t state = makeExpectedFull(relL, relH, GPIO_PIN_RESET, GPIO_PIN_RESET);

    /* Shutdown tests focus on relay order. PRE/BOOST are checked only at final shutdown checkpoint. */
    state.preCharge = ignorePin();
    state.boostPwr = ignorePin();

    return state;
}

static ExpectedSnapshot_t makeExpectedShutdownActive(GPIO_PinState relL,
                                                     GPIO_PinState relH,
                                                     bool checkAuxiliaries)
{
    ExpectedSnapshot_t state = makeExpectedFull(relL,
                                                relH,
                                                checkAuxiliaries ? GPIO_PIN_RESET : GPIO_PIN_RESET,
                                                checkAuxiliaries ? GPIO_PIN_RESET : GPIO_PIN_RESET);

    state.shutdownN = expectPin(GPIO_PIN_RESET);

    if (!checkAuxiliaries)
    {
        state.preCharge = ignorePin();
        state.boostPwr = ignorePin();
    }

    return state;
}

static void setCurrentDirectionForShutdownTest(ShutdownCurrentDirection_t direction)
{
    if (direction == SHUTDOWN_CURRENT_HIGH)
    {
        GreenPakFixture_SetCurrentDirectionHigh();
    }
    else
    {
        GreenPakFixture_SetCurrentDirectionLow();
    }
}

static void setRelayEnablesForClosedState(bool closeL, bool closeH)
{
    ManualControl_SetRelayEnableL(closeL);
    ManualControl_SetRelayEnableH(closeH);
}

static void getExpectedAfterShutdownStart(bool closeL,
                                          bool closeH,
                                          ShutdownCurrentDirection_t direction,
                                          GPIO_PinState *immediateRelL,
                                          GPIO_PinState *immediateRelH,
                                          GPIO_PinState *delayedRelL,
                                          GPIO_PinState *delayedRelH)
{
    GPIO_PinState relLBefore = closeL ? GPIO_PIN_SET : GPIO_PIN_RESET;
    GPIO_PinState relHBefore = closeH ? GPIO_PIN_SET : GPIO_PIN_RESET;

    GPIO_PinState relLImmediate = relLBefore;
    GPIO_PinState relHImmediate = relHBefore;

    if (direction == SHUTDOWN_CURRENT_HIGH)
    {
        /* High current-direction: REL_H opens immediately, REL_L opens after delay. */
        relHImmediate = GPIO_PIN_RESET;
    }
    else
    {
        /* Low current-direction: REL_L opens immediately, REL_H opens after delay. */
        relLImmediate = GPIO_PIN_RESET;
    }

    if (immediateRelL != NULL) { *immediateRelL = relLImmediate; }
    if (immediateRelH != NULL) { *immediateRelH = relHImmediate; }
    if (delayedRelL != NULL) { *delayedRelL = GPIO_PIN_RESET; }
    if (delayedRelH != NULL) { *delayedRelH = GPIO_PIN_RESET; }
}

static void applyShutdownPerturbation(ShutdownCurrentDirection_t originalDirection)
{
    /* Try to disturb the sequence shortly after it has started. */
    ManualControl_SetShutdownReleased(true);

    if (originalDirection == SHUTDOWN_CURRENT_HIGH)
    {
        GreenPakFixture_SetCurrentDirectionLow();
    }
    else
    {
        GreenPakFixture_SetCurrentDirectionHigh();
    }

    ManualControl_SetRelayEnableL(false);
    ManualControl_SetRelayEnableH(false);

    ManualControl_SetResetReleased(false);
    TestContext_DelayUs(100u);
    ManualControl_SetResetReleased(true);

    ManualControl_SetRelayEnableL(true);
    ManualControl_SetRelayEnableH(true);
}

static void runShutdownSequenceSubtest(TestReport_t *report,
                                       bool closeL,
                                       bool closeH,
                                       ShutdownCurrentDirection_t direction,
                                       ShutdownPerturbation_t perturbation)
{
    GPIO_PinState immediateRelL;
    GPIO_PinState immediateRelH;
    GPIO_PinState delayedRelL;
    GPIO_PinState delayedRelH;

    bool defaultOk = GreenPakDefault_Apply();

    setCurrentDirectionForShutdownTest(direction);
    setRelayEnablesForClosedState(closeL, closeH);

    TestContext_DelayMsWithService(100u);
    addCheckpoint(report,
                  "0 ms",
                  "Initial closed state",
                  makeExpectedRelayClosedForShutdownTest(closeL ? GPIO_PIN_SET : GPIO_PIN_RESET,
                                                         closeH ? GPIO_PIN_SET : GPIO_PIN_RESET),
                  defaultOk);

    getExpectedAfterShutdownStart(closeL,
                                  closeH,
                                  direction,
                                  &immediateRelL,
                                  &immediateRelH,
                                  &delayedRelL,
                                  &delayedRelH);

    ManualControl_SetShutdownReleased(false);

    TestContext_DelayUs(100u);
    addCheckpoint(report,
                  "100 us",
                  "Shutdown asserted, immediate relay opened",
                  makeExpectedShutdownActive(immediateRelL, immediateRelH, false),
                  true);

    if (perturbation == SHUTDOWN_PERTURB_AFTER_START)
    {
        applyShutdownPerturbation(direction);
    }

    TestContext_DelayMsWithService(20u);
    addCheckpoint(report,
                  "+20 ms",
                  "Before delayed relay opening",
                  makeExpectedShutdownActive(immediateRelL, immediateRelH, false),
                  true);

    TestContext_DelayMsWithService(10u);
    addCheckpoint(report,
                  "+10 ms",
                  "Delayed relay opened",
                  makeExpectedShutdownActive(delayedRelL, delayedRelH, false),
                  true);

    TestContext_DelayMsWithService(60u);
    addCheckpoint(report,
                  "+60 ms",
                  "Auxiliaries low after shutdown sequence",
                  makeExpectedShutdownActive(GPIO_PIN_RESET, GPIO_PIN_RESET, true),
                  true);
}

typedef void (*ShutdownSubtestFunction_t)(TestReport_t *report);

typedef struct
{
    const char *number;
    const char *name;
    ShutdownSubtestFunction_t run;
} ShutdownSubtest_t;

static void runShutdownSubtest_Both_DirLow(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, true, true, SHUTDOWN_CURRENT_LOW, SHUTDOWN_PERTURB_NONE);
}

static void runShutdownSubtest_Both_DirHigh(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, true, true, SHUTDOWN_CURRENT_HIGH, SHUTDOWN_PERTURB_NONE);
}

static void runShutdownSubtest_LOnly_DirLow(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, true, false, SHUTDOWN_CURRENT_LOW, SHUTDOWN_PERTURB_NONE);
}

static void runShutdownSubtest_LOnly_DirHigh(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, true, false, SHUTDOWN_CURRENT_HIGH, SHUTDOWN_PERTURB_NONE);
}

static void runShutdownSubtest_HOnly_DirHigh(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, false, true, SHUTDOWN_CURRENT_HIGH, SHUTDOWN_PERTURB_NONE);
}

static void runShutdownSubtest_HOnly_DirLow(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, false, true, SHUTDOWN_CURRENT_LOW, SHUTDOWN_PERTURB_NONE);
}

static void runShutdownSubtest_Both_DirLow_Perturbed(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, true, true, SHUTDOWN_CURRENT_LOW, SHUTDOWN_PERTURB_AFTER_START);
}

static void runShutdownSubtest_Both_DirHigh_Perturbed(TestReport_t *report)
{
    runShutdownSequenceSubtest(report, true, true, SHUTDOWN_CURRENT_HIGH, SHUTDOWN_PERTURB_AFTER_START);
}

static const ShutdownSubtest_t s_shutdownSubtests[] =
{
    { "3.1", "Both relays closed, current direction LOW", runShutdownSubtest_Both_DirLow },
    { "3.2", "Both relays closed, current direction HIGH", runShutdownSubtest_Both_DirHigh },
    { "3.3", "L relay only, current direction LOW", runShutdownSubtest_LOnly_DirLow },
    { "3.4", "L relay only, current direction HIGH", runShutdownSubtest_LOnly_DirHigh },
    { "3.5", "H relay only, current direction HIGH", runShutdownSubtest_HOnly_DirHigh },
    { "3.6", "H relay only, current direction LOW", runShutdownSubtest_HOnly_DirLow },
    { "3.7", "Both relays, current direction LOW, perturb after shutdown", runShutdownSubtest_Both_DirLow_Perturbed },
    { "3.8", "Both relays, current direction HIGH, perturb after shutdown", runShutdownSubtest_Both_DirHigh_Perturbed },
};

static TestResult_t Test_VerifyRelayShutdownSequence(TestRunMode_t mode)
{
    bool allOk = true;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        printReportHeader("3",
                          "Relay shutdown sequence",
                          "Verify relay opening order and final auxiliary outputs during shutdown.");
        printf("Note: shutdown latch/reset-release rules are intentionally kept for separate tests.\r\n");
    }

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_shutdownSubtests) / sizeof(s_shutdownSubtests[0])); i++)
    {
        TestReport_t report = {0};
        const ShutdownSubtest_t *subtest = &s_shutdownSubtests[i];

        subtest->run(&report);
        bool ok = reportPassed(&report);
        allOk &= ok;

        if ((mode == TEST_RUN_MODE_DETAILED) || !ok)
        {
            if (mode == TEST_RUN_MODE_RUN_ALL)
            {
                printReportHeader(subtest->number,
                                  subtest->name,
                                  "Verify shutdown relay opening order for this condition.");
                printf("Note: shutdown latch/reset-release rules are intentionally kept for separate tests.\r\n");
            }
            else
            {
                printf("\r\nSub-test %s: %s\r\n", subtest->number, subtest->name);
            }
            printReport(&report, mode == TEST_RUN_MODE_RUN_ALL);
            printf("Test %s result: %s\r\n", subtest->number, ok ? "PASS" : "FAIL");
        }
        else
        {
            printCompletedSubtestSummary(subtest->number, subtest->name, ok);
        }
    }

    bool finalDefaultOk = GreenPakDefault_Apply();
    allOk &= finalDefaultOk;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        TestReport_t report = {0};
        addCheckpoint(&report,
                      "0 ms",
                      "Final default state",
                      makeExpectedFull(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                      finalDefaultOk);
        printf("\r\nFinal default-state check\r\n");
        printReport(&report, false);
        printf("Final default-state result: %s\r\n", reportPassed(&report) ? "PASS" : "FAIL");
        printf("\r\nTest 3 result: %s\r\n", allOk ? "PASS" : "FAIL");
    }
    else if (!allOk)
    {
        printf("Test 3 failed\r\n");
    }

    return allOk ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}


typedef struct
{
    bool check;
    uint32_t expectedMv;
    uint32_t toleranceMv;
} ExpectedVoltage_t;

typedef struct
{
    const char *timeText;
    const char *label;
    ExpectedVoltage_t db0RelayVoltage;
    ExpectedVoltage_t db1RelayVoltage;
    bool checkTempSane;
    GreenPakHost_AnalogValues_t actual;
    bool pass;
} AnalogCheckpoint_t;

typedef struct
{
    AnalogCheckpoint_t items[ANALOG_MAX_CHECKPOINTS];
    uint32_t count;
    bool overflow;
} AnalogReport_t;

static ExpectedVoltage_t expectVoltageMv(uint32_t expectedMv)
{
    ExpectedVoltage_t voltage;
    voltage.check = true;
    voltage.expectedMv = expectedMv;
    voltage.toleranceMv = ANALOG_RELAY_VOLTAGE_TOLERANCE_MV;
    return voltage;
}

static ExpectedVoltage_t ignoreVoltage(void)
{
    ExpectedVoltage_t voltage;
    voltage.check = false;
    voltage.expectedMv = 0u;
    voltage.toleranceMv = 0u;
    return voltage;
}

static uint32_t adcRawToAdcInputMv(uint16_t raw)
{
    return (uint32_t)(((uint64_t)raw * ADC_VREF_MV + (ADC_RAW_MAX / 2u)) / ADC_RAW_MAX);
}

static uint32_t adcRawToRelayVoltageMv(uint16_t raw)
{
    return adcRawToAdcInputMv(raw) * RELAY_DIVIDER_NUMERATOR;
}

static int32_t temperatureRawToCentiC(uint16_t raw)
{
    int32_t sensorUv = (int32_t)(((uint64_t)raw * (uint64_t)ADC_VREF_MV * 1000u +
                                  (ADC_RAW_MAX / 2u)) /
                                 ADC_RAW_MAX);
    int32_t deltaUv = (int32_t)TEMP_SENSOR_25C_UV - sensorUv;

    return (deltaUv * 100) / (int32_t)TEMP_SENSOR_SLOPE_UV_C;
}

static uint32_t absDiffU32(uint32_t a, uint32_t b)
{
    return (a >= b) ? (a - b) : (b - a);
}

static bool voltageMatches(uint16_t raw, ExpectedVoltage_t expected)
{
    if (!expected.check)
    {
        return true;
    }

    uint32_t actualMv = adcRawToRelayVoltageMv(raw);
    return absDiffU32(actualMv, expected.expectedMv) <= expected.toleranceMv;
}

static bool temperatureLooksSane(uint16_t raw)
{
    int32_t centiC = temperatureRawToCentiC(raw);

    /* Broad sanity window. This test is not intended to verify temp calibration. */
    return (centiC > -4000) && (centiC < 12500);
}

static bool analogCheckpointMatches(const AnalogCheckpoint_t *checkpoint)
{
    if ((checkpoint == NULL) || (checkpoint->actual.status != HAL_OK))
    {
        return false;
    }

    bool ok = true;
    ok &= voltageMatches(checkpoint->actual.relayPowerContinuousRaw,
                         checkpoint->db0RelayVoltage);
    ok &= voltageMatches(checkpoint->actual.relayPowerBoostRaw,
                         checkpoint->db1RelayVoltage);

    if (checkpoint->checkTempSane)
    {
        ok &= temperatureLooksSane(checkpoint->actual.temperatureRaw);
    }

    return ok;
}

static void addAnalogCheckpoint(AnalogReport_t *report,
                                const char *timeText,
                                const char *label,
                                ExpectedVoltage_t expectedDb0,
                                ExpectedVoltage_t expectedDb1,
                                bool checkTempSane,
                                bool prerequisiteOk)
{
    if (report == NULL)
    {
        return;
    }

    if (report->count >= ANALOG_MAX_CHECKPOINTS)
    {
        report->overflow = true;
        return;
    }

    AnalogCheckpoint_t *item = &report->items[report->count++];
    item->timeText = timeText;
    item->label = label;
    item->db0RelayVoltage = expectedDb0;
    item->db1RelayVoltage = expectedDb1;
    item->checkTempSane = checkTempSane;
    item->actual.status = GreenPakHost_ReadAnalogValues(&item->actual);
    item->pass = prerequisiteOk && analogCheckpointMatches(item);
}

static bool analogReportPassed(const AnalogReport_t *report)
{
    bool ok = (report != NULL) && !report->overflow;

    if (report != NULL)
    {
        for (uint32_t i = 0u; i < report->count; i++)
        {
            ok &= report->items[i].pass;
        }
    }

    return ok;
}

static void printVoltageExpected(ExpectedVoltage_t expected)
{
    if (!expected.check)
    {
        printf("ignored");
        return;
    }

    printf("%lu.%03lu V +/- %lu.%03lu V",
           (unsigned long)(expected.expectedMv / 1000u),
           (unsigned long)(expected.expectedMv % 1000u),
           (unsigned long)(expected.toleranceMv / 1000u),
           (unsigned long)(expected.toleranceMv % 1000u));
}

static void printRelayVoltageMeasurement(const char *name, uint16_t raw)
{
    uint32_t adcMv = adcRawToAdcInputMv(raw);
    uint32_t relayMv = adcRawToRelayVoltageMv(raw);

    printf("    %-28s raw 0x%04X / %5u  ADC %lu.%03lu V  RelayV %lu.%03lu V\r\n",
           name,
           raw,
           raw,
           (unsigned long)(adcMv / 1000u),
           (unsigned long)(adcMv % 1000u),
           (unsigned long)(relayMv / 1000u),
           (unsigned long)(relayMv % 1000u));
}

static void printTemperatureMeasurement(uint16_t raw)
{
    uint32_t sensorMv = adcRawToAdcInputMv(raw);
    int32_t centiC = temperatureRawToCentiC(raw);
    char sign = '+';

    if (centiC < 0)
    {
        sign = '-';
        centiC = -centiC;
    }

    printf("    %-28s raw 0x%04X / %5u  sensor %lu.%03lu V  temp %c%ld.%02ld C\r\n",
           "DB2 Temperature",
           raw,
           raw,
           (unsigned long)(sensorMv / 1000u),
           (unsigned long)(sensorMv % 1000u),
           sign,
           (long)(centiC / 100),
           (long)(centiC % 100));
}

static void printAnalogReport(const AnalogReport_t *report, bool onlyFailures)
{
    printf("\r\nAnalog checkpoints:\r\n");

    for (uint32_t i = 0u; i < report->count; i++)
    {
        const AnalogCheckpoint_t *item = &report->items[i];

        if (onlyFailures && item->pass)
        {
            continue;
        }

        printf("  %-8s  %-48s %s\r\n",
               item->timeText,
               item->label,
               item->pass ? "PASS" : "FAIL");

        printf("    Expected DB0: ");
        printVoltageExpected(item->db0RelayVoltage);
        printf("\r\n");
        printf("    Expected DB1: ");
        printVoltageExpected(item->db1RelayVoltage);
        printf("\r\n");
        printf("    Expected DB2: %s\r\n", item->checkTempSane ? "sane range" : "not checked");
        printf("    I2C status  : %s\r\n", item->actual.status == HAL_OK ? "OK" : "FAIL");
        printRelayVoltageMeasurement("DB0 Relay pwr continuous", item->actual.relayPowerContinuousRaw);
        printRelayVoltageMeasurement("DB1 Relay pwr during BOOST", item->actual.relayPowerBoostRaw);
        printTemperatureMeasurement(item->actual.temperatureRaw);
    }

    if (report->overflow)
    {
        printf("  WARNING: analog report checkpoint buffer overflowed. Increase ANALOG_MAX_CHECKPOINTS.\r\n");
    }
}

static bool setRelayVoltageForAnalogTest(uint32_t relayVoltageMv)
{
    return GreenPakFixture_SetRelayPwrVoltageSimulatedMv(relayVoltageMv,
                                                         NULL,
                                                         NULL,
                                                         NULL) == GREENPAK_FIXTURE_OK;
}

static bool waitAnalogPinLevel(TestPin_t pin, GPIO_PinState expected, uint32_t timeoutMs)
{
    uint32_t start = HAL_GetTick();

    while ((HAL_GetTick() - start) <= timeoutMs)
    {
        if (TestContext_ReadPin(pin) == expected)
        {
            return true;
        }

        TestContext_DelayMsWithService(1u);
    }

    return false;
}

static void waitForMovingAverageSettle(void)
{
    TestContext_DelayMsWithService(ANALOG_MOVING_AVG_SETTLE_MS);
}

static void applyAnalogDefault(void)
{
    (void)GreenPakDefault_Apply();
    (void)GreenPakHost_SetAdcEnable(true);
    TestContext_DelayMsWithService(ANALOG_ADC_POWER_UP_MS);
}

static void runAnalogAccuracySubtest(AnalogReport_t *report)
{
    static const uint32_t voltagesMv[] = {0u, 5000u, 12000u, 16000u};

    applyAnalogDefault();

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(voltagesMv) / sizeof(voltagesMv[0])); i++)
    {
        bool setOk = setRelayVoltageForAnalogTest(voltagesMv[i]);
        waitForMovingAverageSettle();

        const char *label;
        const char *timeText;
        switch (i)
        {
            case 0u: label = "DB0 accuracy at 0 V"; timeText = "1.2 s"; break;
            case 1u: label = "DB0 accuracy at 5 V"; timeText = "+1.2 s"; break;
            case 2u: label = "DB0 accuracy at 12 V"; timeText = "+1.2 s"; break;
            default: label = "DB0 accuracy at 16 V"; timeText = "+1.2 s"; break;
        }

        addAnalogCheckpoint(report,
                            timeText,
                            label,
                            expectVoltageMv(voltagesMv[i]),
                            ignoreVoltage(),
                            true,
                            setOk);
    }
}

static void runAnalogAdcOnOffSubtest(AnalogReport_t *report)
{
    bool ok = true;

    applyAnalogDefault();

    ok &= setRelayVoltageForAnalogTest(4000u);
    waitForMovingAverageSettle();
    addAnalogCheckpoint(report,
                        "1.2 s",
                        "ADC enabled, DB0 follows 4 V",
                        expectVoltageMv(4000u),
                        ignoreVoltage(),
                        true,
                        ok);

    ok &= (GreenPakHost_SetAdcEnable(false) == HAL_OK);
    TestContext_DelayMsWithService(5u);
    ok &= setRelayVoltageForAnalogTest(14000u);
    TestContext_DelayMsWithService(ANALOG_ADC_OFF_HOLD_MS);
    addAnalogCheckpoint(report,
                        "+40 ms",
                        "ADC disabled, DB0 remains at old 4 V value",
                        expectVoltageMv(4000u),
                        ignoreVoltage(),
                        true,
                        ok);

    ok &= (GreenPakHost_SetAdcEnable(true) == HAL_OK);
    waitForMovingAverageSettle();
    addAnalogCheckpoint(report,
                        "+1.2 s",
                        "ADC re-enabled, DB0 follows 14 V",
                        expectVoltageMv(14000u),
                        ignoreVoltage(),
                        true,
                        ok);
}

static void runAnalogDb1BoostGateSubtest(AnalogReport_t *report)
{
    bool ok = true;

    applyAnalogDefault();

    /*
     * Set the voltage before requesting a relay close, then use the actual
     * BOOST_PWR pin to decide when the capture window has ended. This avoids
     * assuming an exact BOOST duration in the test firmware.
     */
    ok &= setRelayVoltageForAnalogTest(5000u);
    ManualControl_SetRelayEnableL(true);
    ok &= waitAnalogPinLevel(TEST_PIN_BOOST_PWR, GPIO_PIN_SET, ANALOG_BOOST_HIGH_TIMEOUT_MS);
    ok &= waitAnalogPinLevel(TEST_PIN_BOOST_PWR, GPIO_PIN_RESET, ANALOG_BOOST_LOW_TIMEOUT_MS);

    ok &= setRelayVoltageForAnalogTest(12000u);
    TestContext_DelayMsWithService(ANALOG_DB1_AFTER_BOOST_HOLD_MS);
    addAnalogCheckpoint(report,
                        "+1.2 s",
                        "DB1 held 5 V after BOOST low while DB0 follows 12 V",
                        expectVoltageMv(12000u),
                        expectVoltageMv(5000u),
                        true,
                        ok);

    ok &= GreenPakDefault_Apply();
    ok &= (GreenPakHost_SetAdcEnable(true) == HAL_OK);
    TestContext_DelayMsWithService(ANALOG_ADC_POWER_UP_MS);

    ok &= setRelayVoltageForAnalogTest(12000u);
    ManualControl_SetRelayEnableH(true);
    ok &= waitAnalogPinLevel(TEST_PIN_BOOST_PWR, GPIO_PIN_SET, ANALOG_BOOST_HIGH_TIMEOUT_MS);
    ok &= waitAnalogPinLevel(TEST_PIN_BOOST_PWR, GPIO_PIN_RESET, ANALOG_BOOST_LOW_TIMEOUT_MS);
    addAnalogCheckpoint(report,
                        "BOOST done",
                        "DB1 updates to 12 V during next BOOST window",
                        expectVoltageMv(12000u),
                        expectVoltageMv(12000u),
                        true,
                        ok);
}

typedef void (*AnalogSubtestFunction_t)(AnalogReport_t *report);

typedef struct
{
    const char *number;
    const char *name;
    AnalogSubtestFunction_t run;
} AnalogSubtest_t;

static void runAnalogSubtest_Accuracy(AnalogReport_t *report) { runAnalogAccuracySubtest(report); }
static void runAnalogSubtest_AdcOnOff(AnalogReport_t *report) { runAnalogAdcOnOffSubtest(report); }
static void runAnalogSubtest_Db1BoostGate(AnalogReport_t *report) { runAnalogDb1BoostGateSubtest(report); }

static const AnalogSubtest_t s_analogSubtests[] =
{
    { "4.1", "DB0 voltage accuracy", runAnalogSubtest_Accuracy },
    { "4.2", "ADC enable/disable freezes and resumes DB0", runAnalogSubtest_AdcOnOff },
    { "4.3", "DB1 only updates while BOOST is high", runAnalogSubtest_Db1BoostGate },
};

static TestResult_t Test_VerifyAdcAndDataBufferBehavior(TestRunMode_t mode)
{
    bool allOk = true;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        printReportHeader("4",
                          "ADC and data-buffer behavior",
                          "Verify ADC enable, relay-voltage accuracy, and BOOST-gated Data Buffer1 updates.");
        printf("Note: ADC sampling is 2.5 ksps total, split across two channels, about 1.25 ksps per channel.\r\n");
        printf("      Voltage accuracy checks wait 1.2 s to allow the moving-average buffers to settle.\r\n");
    }

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_analogSubtests) / sizeof(s_analogSubtests[0])); i++)
    {
        AnalogReport_t report = {0};
        const AnalogSubtest_t *subtest = &s_analogSubtests[i];

        subtest->run(&report);
        bool ok = analogReportPassed(&report);
        allOk &= ok;

        if ((mode == TEST_RUN_MODE_DETAILED) || !ok)
        {
            if (mode == TEST_RUN_MODE_RUN_ALL)
            {
                printReportHeader(subtest->number,
                                  subtest->name,
                                  "Verify analog/ADC behavior for this condition.");
            }
            else
            {
                printf("\r\nSub-test %s: %s\r\n", subtest->number, subtest->name);
            }
            printAnalogReport(&report, mode == TEST_RUN_MODE_RUN_ALL);
            printf("Test %s result: %s\r\n", subtest->number, ok ? "PASS" : "FAIL");
        }
        else
        {
            printCompletedSubtestSummary(subtest->number, subtest->name, ok);
        }
    }

    (void)GreenPakHost_SetAdcEnable(false);
    (void)GreenPakFixture_SetRelayPwrVoltageZero();
    bool finalDefaultOk = GreenPakDefault_Apply();
    allOk &= finalDefaultOk;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        TestReport_t report = {0};
        addCheckpoint(&report,
                      "0 ms",
                      "Final default state",
                      makeExpectedFull(GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET, GPIO_PIN_RESET),
                      finalDefaultOk);
        printf("\r\nFinal default-state check\r\n");
        printReport(&report, false);
        printf("Final default-state result: %s\r\n", reportPassed(&report) ? "PASS" : "FAIL");
        printf("\r\nTest 4 result: %s\r\n", allOk ? "PASS" : "FAIL");
    }
    else if (!allOk)
    {
        printf("Test 4 failed\r\n");
    }

    return allOk ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}


typedef struct
{
    bool check;
    bool expected;
} ExpectedSignalBit_t;

typedef struct
{
    ExpectedSignalBit_t relayL;
    ExpectedSignalBit_t relayH;
    ExpectedSignalBit_t shutdownLatched;
    ExpectedSignalBit_t resetAllowed;
    ExpectedSignalBit_t currentDirection;
    ExpectedSignalBit_t enableL;
    ExpectedSignalBit_t enableH;
    ExpectedSignalBit_t wdtLatched;
} ExpectedSignalReadback_t;

typedef struct
{
    const char *timeText;
    const char *label;
    ExpectedSignalReadback_t expected;
    uint8_t actual;
    HAL_StatusTypeDef i2cStatus;
    bool pass;
} SignalReadbackCheckpoint_t;

#define SIGNAL_READBACK_MAX_CHECKPOINTS 24u

typedef struct
{
    SignalReadbackCheckpoint_t items[SIGNAL_READBACK_MAX_CHECKPOINTS];
    uint32_t count;
    bool overflow;
} SignalReadbackReport_t;

static ExpectedSignalBit_t expectSignalBit(bool expected)
{
    ExpectedSignalBit_t bit;
    bit.check = true;
    bit.expected = expected;
    return bit;
}

static ExpectedSignalBit_t ignoreSignalBit(void)
{
    ExpectedSignalBit_t bit;
    bit.check = false;
    bit.expected = false;
    return bit;
}

static ExpectedSignalReadback_t makeSignalExpectedAll(bool relayL,
                                                      bool relayH,
                                                      bool shutdownLatched,
                                                      bool resetAllowed,
                                                      bool currentDirection,
                                                      bool enableL,
                                                      bool enableH,
                                                      bool wdtLatched)
{
    ExpectedSignalReadback_t expected;

    expected.relayL = expectSignalBit(relayL);
    expected.relayH = expectSignalBit(relayH);
    expected.shutdownLatched = expectSignalBit(shutdownLatched);
    expected.resetAllowed = expectSignalBit(resetAllowed);
    expected.currentDirection = expectSignalBit(currentDirection);
    expected.enableL = expectSignalBit(enableL);
    expected.enableH = expectSignalBit(enableH);
    expected.wdtLatched = expectSignalBit(wdtLatched);

    return expected;
}

static bool signalBitValue(uint8_t raw, uint8_t bit)
{
    return ((raw & (uint8_t)(1u << bit)) != 0u);
}

static bool signalBitMatches(uint8_t raw, uint8_t bit, ExpectedSignalBit_t expected)
{
    if (!expected.check)
    {
        return true;
    }

    return signalBitValue(raw, bit) == expected.expected;
}

static bool signalReadbackMatches(uint8_t raw, const ExpectedSignalReadback_t *expected)
{
    return signalBitMatches(raw, 0u, expected->relayL) &&
           signalBitMatches(raw, 1u, expected->relayH) &&
           signalBitMatches(raw, 2u, expected->shutdownLatched) &&
           signalBitMatches(raw, 3u, expected->resetAllowed) &&
           signalBitMatches(raw, 4u, expected->currentDirection) &&
           signalBitMatches(raw, 5u, expected->enableL) &&
           signalBitMatches(raw, 6u, expected->enableH) &&
           signalBitMatches(raw, 7u, expected->wdtLatched);
}

static void addSignalReadbackCheckpoint(SignalReadbackReport_t *report,
                                        const char *timeText,
                                        const char *label,
                                        ExpectedSignalReadback_t expected,
                                        bool prerequisiteOk)
{
    if (report == NULL)
    {
        return;
    }

    if (report->count >= SIGNAL_READBACK_MAX_CHECKPOINTS)
    {
        report->overflow = true;
        return;
    }

    SignalReadbackCheckpoint_t *item = &report->items[report->count++];

    item->timeText = timeText;
    item->label = label;
    item->expected = expected;
    item->actual = 0u;
    item->i2cStatus = GreenPakHost_ReadSignalReadback(&item->actual);

    /*
     * The signal-readback checkpoint should verify the readback register itself.
     * Some setup/default-state helpers may return false because of unrelated
     * output checks, while the readback bits can still be exactly correct.
     * Treat prerequisiteOk only as a guard for running the sequence, not as part
     * of the signal-readback comparison result.
     */
    (void)prerequisiteOk;
    item->pass = (item->i2cStatus == HAL_OK) && signalReadbackMatches(item->actual, &item->expected);
}

static void printExpectedSignalBit(const char *name, ExpectedSignalBit_t bit)
{
    printf("    %-16s : ", name);
    if (bit.check)
    {
        printf("%s\r\n", bit.expected ? "ON" : "OFF");
    }
    else
    {
        printf("ignored\r\n");
    }
}

static void printExpectedSignalReadback(const ExpectedSignalReadback_t *expected)
{
    printf("    Expected decoded:\r\n");
    printExpectedSignalBit("IN0 Relay L", expected->relayL);
    printExpectedSignalBit("IN1 Relay H", expected->relayH);
    printExpectedSignalBit("IN2 Sdn latched", expected->shutdownLatched);
    printExpectedSignalBit("IN3 Reset allowed", expected->resetAllowed);
    printExpectedSignalBit("IN4 Current dir", expected->currentDirection);
    printExpectedSignalBit("IN5 Enable L", expected->enableL);
    printExpectedSignalBit("IN6 Enable H", expected->enableH);
    printExpectedSignalBit("IN7 WDT latched", expected->wdtLatched);
}

static void printActualSignalReadback(uint8_t raw)
{
    printf("    Actual raw 0x0062: 0x%02X\r\n", raw);
    printf("    Actual decoded  : IN7 WDT=%s IN6 EN_H=%s IN5 EN_L=%s IN4 DIR=%s IN3 RST_OK=%s IN2 SDN=%s IN1 REL_H=%s IN0 REL_L=%s\r\n",
           signalBitValue(raw, 7u) ? "ON" : "OFF",
           signalBitValue(raw, 6u) ? "ON" : "OFF",
           signalBitValue(raw, 5u) ? "ON" : "OFF",
           signalBitValue(raw, 4u) ? "ON" : "OFF",
           signalBitValue(raw, 3u) ? "ON" : "OFF",
           signalBitValue(raw, 2u) ? "ON" : "OFF",
           signalBitValue(raw, 1u) ? "ON" : "OFF",
           signalBitValue(raw, 0u) ? "ON" : "OFF");
}

static void printSignalReadbackReport(const SignalReadbackReport_t *report, bool onlyFailures)
{
    printf("\r\nSignal readback checkpoints:\r\n");
    for (uint32_t i = 0u; i < report->count; i++)
    {
        const SignalReadbackCheckpoint_t *item = &report->items[i];

        if (onlyFailures && item->pass)
        {
            continue;
        }

        printf("  %-8s  %-48s %s\r\n",
               item->timeText,
               item->label,
               item->pass ? "PASS" : "FAIL");

        if (!item->pass)
        {
            printf("    I2C status: %s\r\n", (item->i2cStatus == HAL_OK) ? "OK" : "FAIL");
            printExpectedSignalReadback(&item->expected);
        }

        if ((!onlyFailures) || !item->pass)
        {
            printActualSignalReadback(item->actual);
        }
    }

    if (report->overflow)
    {
        printf("  WARNING: signal readback checkpoint buffer overflowed. Increase SIGNAL_READBACK_MAX_CHECKPOINTS.\r\n");
    }
}

static bool signalReadbackReportPassed(const SignalReadbackReport_t *report)
{
    bool ok = !report->overflow;

    for (uint32_t i = 0u; i < report->count; i++)
    {
        ok &= report->items[i].pass;
    }

    return ok;
}

static ExpectedSignalReadback_t signalExpectedDefaultWithDirection(bool currentDirection)
{
    return makeSignalExpectedAll(false,  /* IN0 Relay L */
                                 false,  /* IN1 Relay H */
                                 false,  /* IN2 Sdn latched */
                                 true,   /* IN3 Reset allowed */
                                 currentDirection,
                                 false,  /* IN5 Enable L */
                                 false,  /* IN6 Enable H */
                                 false); /* IN7 WDT latched */
}

static void runReadbackSubtest_Default(SignalReadbackReport_t *report)
{
    bool ok = GreenPakDefault_Apply();
    GreenPakFixture_SetCurrentDirectionLow();
    TestContext_DelayMsWithService(2u);

    addSignalReadbackCheckpoint(report,
                                "0 ms",
                                "Default readback state",
                                signalExpectedDefaultWithDirection(false),
                                ok);
}

static void runReadbackSubtest_McuDrivenInputs(SignalReadbackReport_t *report)
{
    bool ok = GreenPakDefault_Apply();

    GreenPakFixture_SetCurrentDirectionLow();
    ManualControl_SetRelayEnableL(false);
    ManualControl_SetRelayEnableH(false);
    TestContext_DelayMsWithService(2u);
    addSignalReadbackCheckpoint(report,
                                "0 ms",
                                "Inputs low/default",
                                signalExpectedDefaultWithDirection(false),
                                ok);

    GreenPakFixture_SetCurrentDirectionHigh();
    TestContext_DelayMsWithService(2u);
    addSignalReadbackCheckpoint(report,
                                "+2 ms",
                                "Current direction high",
                                makeSignalExpectedAll(false, false, false, true, true, false, false, false),
                                true);

    ManualControl_SetRelayEnableL(true);
    TestContext_DelayUs(100u);
    addSignalReadbackCheckpoint(report,
                                "100 us",
                                "Enable L high readback",
                                makeSignalExpectedAll(false, false, false, true, true, true, false, false),
                                true);

    ManualControl_SetRelayEnableH(true);
    TestContext_DelayUs(100u);
    addSignalReadbackCheckpoint(report,
                                "100 us",
                                "Enable H high readback",
                                makeSignalExpectedAll(false, false, false, true, true, true, true, false),
                                true);
}

static void runReadbackSubtest_RelayOutputs(SignalReadbackReport_t *report)
{
    bool ok = GreenPakDefault_Apply();
    GreenPakFixture_SetCurrentDirectionLow();
    ManualControl_SetRelayEnableL(true);
    TestContext_DelayMsWithService(100u);
    addSignalReadbackCheckpoint(report,
                                "100 ms",
                                "Relay L closed readback",
                                makeSignalExpectedAll(true, false, false, true, false, true, false, false),
                                ok);

    ok &= GreenPakDefault_Apply();
    GreenPakFixture_SetCurrentDirectionLow();
    ManualControl_SetRelayEnableH(true);
    TestContext_DelayMsWithService(100u);
    addSignalReadbackCheckpoint(report,
                                "100 ms",
                                "Relay H closed readback",
                                makeSignalExpectedAll(false, true, false, true, false, false, true, false),
                                ok);

    ok &= GreenPakDefault_Apply();
    GreenPakFixture_SetCurrentDirectionLow();
    ManualControl_SetRelayEnableL(true);
    ManualControl_SetRelayEnableH(true);
    TestContext_DelayMsWithService(100u);
    addSignalReadbackCheckpoint(report,
                                "100 ms",
                                "Both relays closed readback",
                                makeSignalExpectedAll(true, true, false, true, false, true, true, false),
                                ok);
}

static void runReadbackSubtest_ShutdownAndResetAllowed(SignalReadbackReport_t *report)
{
    bool ok = GreenPakDefault_Apply();
    GreenPakFixture_SetCurrentDirectionLow();
    ManualControl_SetRelayEnableL(true);
    ManualControl_SetRelayEnableH(true);
    TestContext_DelayMsWithService(100u);

    ManualControl_SetShutdownReleased(false);
    TestContext_DelayUs(100u);
    addSignalReadbackCheckpoint(report,
                                "100 us",
                                "Shutdown latched, reset not allowed",
                                makeSignalExpectedAll(false, true, true, false, false, true, true, false),
                                ok);

    TestContext_DelayMsWithService(60u);
    addSignalReadbackCheckpoint(report,
                                "+60 ms",
                                "Reset allowed after latch delay",
                                makeSignalExpectedAll(false, false, true, true, false, true, true, false),
                                true);
}

static void runReadbackSubtest_WdtLatched(SignalReadbackReport_t *report)
{
    bool ok = GreenPakDefault_Apply();
    GreenPakFixture_SetCurrentDirectionLow();

    ok &= (GreenPakHost_SetWdtAutoToggle(false) == HAL_OK);
    TestContext_DelayMsWithService(2500u);
    addSignalReadbackCheckpoint(report,
                                "2500 ms",
                                "WDT timeout latches WDT and shutdown",
                                makeSignalExpectedAll(false, false, true, true, false, false, false, true),
                                ok);
}

typedef void (*SignalReadbackSubtestFunction_t)(SignalReadbackReport_t *report);

typedef struct
{
    const char *number;
    const char *name;
    SignalReadbackSubtestFunction_t run;
} SignalReadbackSubtest_t;

static void runSignalReadbackSubtest_Default(SignalReadbackReport_t *report) { runReadbackSubtest_Default(report); }
static void runSignalReadbackSubtest_McuInputs(SignalReadbackReport_t *report) { runReadbackSubtest_McuDrivenInputs(report); }
static void runSignalReadbackSubtest_RelayOutputs(SignalReadbackReport_t *report) { runReadbackSubtest_RelayOutputs(report); }
static void runSignalReadbackSubtest_Shutdown(SignalReadbackReport_t *report) { runReadbackSubtest_ShutdownAndResetAllowed(report); }
static void runSignalReadbackSubtest_Wdt(SignalReadbackReport_t *report) { runReadbackSubtest_WdtLatched(report); }

static const SignalReadbackSubtest_t s_signalReadbackSubtests[] =
{
    { "5.1", "Default readback state", runSignalReadbackSubtest_Default },
    { "5.2", "MCU-driven input readback", runSignalReadbackSubtest_McuInputs },
    { "5.3", "Relay output readback", runSignalReadbackSubtest_RelayOutputs },
    { "5.4", "Shutdown latch and reset-allowed readback", runSignalReadbackSubtest_Shutdown },
    { "5.5", "WDT latched readback", runSignalReadbackSubtest_Wdt },
};

static TestResult_t Test_VerifyI2cSignalReadback(TestRunMode_t mode)
{
    bool allOk = true;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        printReportHeader("5",
                          "I2C signal readback",
                          "Verify all eight signal-readback bits in register 0x0062.");
        printf("Signal map: IN0 Relay L, IN1 Relay H, IN2 Sdn latched, IN3 Reset allowed, IN4 Current direction, IN5 Enable L, IN6 Enable H, IN7 WDT latched.\r\n");
    }

    for (uint32_t i = 0u; i < (uint32_t)(sizeof(s_signalReadbackSubtests) / sizeof(s_signalReadbackSubtests[0])); i++)
    {
        SignalReadbackReport_t report = {0};
        const SignalReadbackSubtest_t *subtest = &s_signalReadbackSubtests[i];

        subtest->run(&report);
        bool ok = signalReadbackReportPassed(&report);
        allOk &= ok;

        if ((mode == TEST_RUN_MODE_DETAILED) || !ok)
        {
            if (mode == TEST_RUN_MODE_RUN_ALL)
            {
                printReportHeader(subtest->number,
                                  subtest->name,
                                  "Verify signal-readback bits for this condition.");
            }
            else
            {
                printf("\r\nSub-test %s: %s\r\n", subtest->number, subtest->name);
            }
            printSignalReadbackReport(&report, mode == TEST_RUN_MODE_RUN_ALL);
            printf("Test %s result: %s\r\n", subtest->number, ok ? "PASS" : "FAIL");
        }
        else
        {
            printCompletedSubtestSummary(subtest->number, subtest->name, ok);
        }
    }

    bool finalDefaultOk = GreenPakDefault_Apply();
    allOk &= finalDefaultOk;

    if (mode == TEST_RUN_MODE_DETAILED)
    {
        SignalReadbackReport_t report = {0};
        GreenPakFixture_SetCurrentDirectionLow();
        TestContext_DelayMsWithService(2u);
        addSignalReadbackCheckpoint(&report,
                                    "0 ms",
                                    "Final default readback state",
                                    signalExpectedDefaultWithDirection(false),
                                    finalDefaultOk);
        printf("\r\nFinal signal-readback default-state check\r\n");
        printSignalReadbackReport(&report, false);
        printf("Final signal-readback default-state result: %s\r\n", signalReadbackReportPassed(&report) ? "PASS" : "FAIL");
        printf("\r\nTest 5 result: %s\r\n", allOk ? "PASS" : "FAIL");
    }
    else if (!allOk)
    {
        printf("Test 5 failed\r\n");
    }

    return allOk ? TEST_RESULT_PASS : TEST_RESULT_FAIL;
}

static const TestCase_t s_tests[] =
{
    {
        .id = 1u,
        .name = "Verify PreChargeEn normal behavior",
        .intention = "Verify PreChargeEn works in a normal relay close sequence.",
        .run = Test_VerifyPreChargeEnNormalBehavior,
    },
    {
        .id = 2u,
        .name = "Verify Boost power normal behavior",
        .intention = "Verify Boost power asserts during normal relay close sequences and releases afterwards.",
        .run = Test_VerifyBoostPowerNormalBehavior,
    },
    {
        .id = 3u,
        .name = "Relay shutdown sequence",
        .intention = "Verify relay opening order and final auxiliary outputs during shutdown.",
        .run = Test_VerifyRelayShutdownSequence,
    },
    {
        .id = 4u,
        .name = "ADC and data-buffer behavior",
        .intention = "Verify ADC enable, relay-voltage accuracy, and BOOST-gated Data Buffer1 updates.",
        .run = Test_VerifyAdcAndDataBufferBehavior,
    },
    {
        .id = 5u,
        .name = "I2C signal readback",
        .intention = "Verify all eight signal-readback bits in register 0x0062.",
        .run = Test_VerifyI2cSignalReadback,
    },
};

const TestCase_t *TestCases_GetList(uint32_t *count)
{
    if (count != NULL)
    {
        *count = (uint32_t)(sizeof(s_tests) / sizeof(s_tests[0]));
    }

    return s_tests;
}

const TestCase_t *TestCases_FindById(uint16_t id)
{
    uint32_t count = 0u;
    const TestCase_t *tests = TestCases_GetList(&count);

    for (uint32_t i = 0u; i < count; i++)
    {
        if (tests[i].id == id)
        {
            return &tests[i];
        }
    }

    return NULL;
}

const char *TestCases_ResultText(TestResult_t result)
{
    switch (result)
    {
        case TEST_RESULT_PASS: return "PASS";
        case TEST_RESULT_FAIL: return "FAIL";
        case TEST_RESULT_SKIP: return "SKIP";
        default: return "UNKNOWN";
    }
}
