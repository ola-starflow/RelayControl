#include "test_cases.h"
#include "greenpak_default.h"
#include "manual_control.h"
#include "test_context.h"

#include <stddef.h>
#include <stdio.h>

#define TEST_MAX_CHECKPOINTS 16u

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
