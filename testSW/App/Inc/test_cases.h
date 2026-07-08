#ifndef TEST_CASES_H
#define TEST_CASES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    TEST_RESULT_PASS = 0,
    TEST_RESULT_FAIL,
    TEST_RESULT_SKIP
} TestResult_t;

typedef enum
{
    TEST_RUN_MODE_DETAILED = 0,
    TEST_RUN_MODE_RUN_ALL
} TestRunMode_t;

typedef TestResult_t (*TestFunction_t)(TestRunMode_t mode);

typedef struct
{
    uint16_t id;
    const char *name;
    const char *intention;
    TestFunction_t run;
} TestCase_t;

const TestCase_t *TestCases_GetList(uint32_t *count);
const TestCase_t *TestCases_FindById(uint16_t id);
const char *TestCases_ResultText(TestResult_t result);

#endif /* TEST_CASES_H */
