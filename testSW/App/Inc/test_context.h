#ifndef TEST_CONTEXT_H
#define TEST_CONTEXT_H

#include "stm32g4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    TEST_PIN_SHUTDOWN_N = 0,
    TEST_PIN_REL_H,
    TEST_PIN_REL_L,
    TEST_PIN_PRE_CHARGE,
    TEST_PIN_BOOST_PWR,
    TEST_PIN_REL_PWR_EN
} TestPin_t;

const char *TestContext_PinName(TestPin_t pin);
GPIO_PinState TestContext_ReadPin(TestPin_t pin);
void TestContext_DelayUs(uint32_t us);
void TestContext_DelayMsWithService(uint32_t ms);
void TestContext_WaitUntilMs(uint32_t targetTickMs);
bool TestContext_ExpectPin(TestPin_t pin, GPIO_PinState expected, const char *detail);
bool TestContext_CheckPin(TestPin_t pin, GPIO_PinState expected);

#endif /* TEST_CONTEXT_H */
