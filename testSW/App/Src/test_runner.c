#include "test_runner.h"
#include "greenpak_default.h"
#include "greenpak_host.h"
#include "test_cases.h"

#include <stdio.h>

#define TEST_PANEL_WIDTH 66u
#define TEST_PANEL_FMT   "%-66.66s"

static void printRule(const char *left, const char *fill, const char *right)
{
    printf("%s", left);
    for (uint32_t i = 0u; i < (TEST_PANEL_WIDTH + 2u); i++)
    {
        printf("%s", fill);
    }
    printf("%s\r\n", right);
}

static void printLine(const char *text)
{
    printf("│ " TEST_PANEL_FMT " │\r\n", (text != NULL) ? text : "");
}

static void clearScreen(void)
{
    printf("\033[2J");
    printf("\033[H");
}

void TestRunner_PrintMenu(const char *message)
{
    clearScreen();
    printRule("┌", "─", "┐");
    char header[96];
    (void)snprintf(header,
                   sizeof(header),
                   "SLG47011 Test interface  >>> SELECTED I2C: 0x%02X <<<",
                   (unsigned int)GreenPakHost_GetSelectedAddress7Bit());
    printLine(header);
    printRule("├", "─", "┤");
    printLine("Tests:");
    uint32_t count = 0u;
    const TestCase_t *tests = TestCases_GetList(&count);
    char line[96];
    for (uint32_t i = 0u; i < count; i++)
    {
        (void)snprintf(line, sizeof(line), "  %u  %s", tests[i].id, tests[i].name);
        printLine(line);
    }
    printLine("");
    printLine("Commands:");
    printLine("  a      run all tests");
    printLine("  number + Enter  run selected test");
    printLine("  l      list tests");
    printLine("  d      apply default state");
    printLine("  n      switch selected I2C device 0x08 / 0x18");
    printLine("  p      refresh");
    printLine("  m      manual interface");
    printLine("  b      back to main menu");
    printRule("├", "─", "┤");
    printLine((message != NULL) ? message : "Ready.");
    printRule("└", "─", "┘");
    printf("> ");
}

void TestRunner_PrintList(void)
{
    uint32_t count = 0u;
    const TestCase_t *tests = TestCases_GetList(&count);

    clearScreen();
    printRule("┌", "─", "┐");
    printLine("SLG47011 Test List");
    printRule("├", "─", "┤");

    for (uint32_t i = 0u; i < count; i++)
    {
        char line[96];
        (void)snprintf(line, sizeof(line), "%3u  %s", tests[i].id, tests[i].name);
        printLine(line);
        (void)snprintf(line, sizeof(line), "     %s", tests[i].intention);
        printLine(line);
    }

    printRule("└", "─", "┘");
    printf("> ");
}

void TestRunner_ApplyDefaultCommand(void)
{
    clearScreen();
    printRule("┌", "─", "┐");
    printLine("Apply GreenPAK default state");
    printRule("├", "─", "┤");
    bool ok = GreenPakDefault_Apply();
    printRule("├", "─", "┤");
    printLine(ok ? "Result: PASS" : "Result: FAIL");
    printRule("└", "─", "┘");
    printf("> ");
}

void TestRunner_RunById(uint16_t id)
{
    const TestCase_t *test = TestCases_FindById(id);

    clearScreen();
    printRule("┌", "─", "┐");

    if (test == NULL)
    {
        printLine("Unknown test number.");
        printRule("└", "─", "┘");
        printf("> ");
        return;
    }

    char line[96];
    (void)snprintf(line, sizeof(line), "Run test %u: %s", test->id, test->name);
    printLine(line);
    printRule("├", "─", "┤");

    TestResult_t result = test->run(TEST_RUN_MODE_DETAILED);

    printRule("├", "─", "┤");
    (void)snprintf(line, sizeof(line), "Result: %s", TestCases_ResultText(result));
    printLine(line);
    printRule("└", "─", "┘");
    printf("> ");
}

void TestRunner_RunAll(void)
{
    uint32_t count = 0u;
    const TestCase_t *tests = TestCases_GetList(&count);
    uint32_t pass = 0u;
    uint32_t fail = 0u;
    uint32_t skip = 0u;

    clearScreen();
    printRule("┌", "─", "┐");
    printLine("Run all SLG47011 tests");
    printRule("├", "─", "┤");
    printLine("Passing tests print summary only. Failing tests print details.");
    printRule("├", "─", "┤");

    for (uint32_t i = 0u; i < count; i++)
    {
        TestResult_t result = tests[i].run(TEST_RUN_MODE_RUN_ALL);
        if (result == TEST_RESULT_PASS) { pass++; }
        else if (result == TEST_RESULT_FAIL) { fail++; }
        else { skip++; }
    }

    printRule("├", "─", "┤");
    char summary[96];
    (void)snprintf(summary, sizeof(summary), "Summary: %lu pass, %lu fail, %lu skip",
                   (unsigned long)pass,
                   (unsigned long)fail,
                   (unsigned long)skip);
    printLine(summary);
    printRule("└", "─", "┘");
    printf("> ");
}
