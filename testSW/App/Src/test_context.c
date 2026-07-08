#include "test_context.h"
#include "greenpak_host.h"
#include "main.h"

#include <stdio.h>

const char *TestContext_PinName(TestPin_t pin)
{
    switch (pin)
    {
        case TEST_PIN_SHUTDOWN_N: return "SHUTDOWN_N";
        case TEST_PIN_REL_H: return "REL_H";
        case TEST_PIN_REL_L: return "REL_L";
        case TEST_PIN_PRE_CHARGE: return "PRE_CHARGE";
        case TEST_PIN_BOOST_PWR: return "BOOST_PWR";
        case TEST_PIN_REL_PWR_EN: return "REL_PWR_EN";
        default: return "UNKNOWN";
    }
}

GPIO_PinState TestContext_ReadPin(TestPin_t pin)
{
    switch (pin)
    {
        case TEST_PIN_SHUTDOWN_N: return HAL_GPIO_ReadPin(REL_SHUTDOWN_N_GPIO_Port, REL_SHUTDOWN_N_Pin);
        case TEST_PIN_REL_H: return HAL_GPIO_ReadPin(REL_H_GPIO_Port, REL_H_Pin);
        case TEST_PIN_REL_L: return HAL_GPIO_ReadPin(REL_L_GPIO_Port, REL_L_Pin);
        case TEST_PIN_PRE_CHARGE: return HAL_GPIO_ReadPin(PRE_CHARGE_EN_GPIO_Port, PRE_CHARGE_EN_Pin);
        case TEST_PIN_BOOST_PWR: return HAL_GPIO_ReadPin(REL_BOOST_PWR_GPIO_Port, REL_BOOST_PWR_Pin);
        case TEST_PIN_REL_PWR_EN: return HAL_GPIO_ReadPin(REL_PWR_EN_GPIO_Port, REL_PWR_EN_Pin);
        default: return GPIO_PIN_RESET;
    }
}

void TestContext_DelayUs(uint32_t us)
{
    if (us == 0u)
    {
        return;
    }

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = (SystemCoreClock / 1000000u) * us;

    while ((uint32_t)(DWT->CYCCNT - start) < cycles)
    {
        /* Busy wait. */
    }
}

void TestContext_DelayMsWithService(uint32_t ms)
{
    uint32_t start = HAL_GetTick();

    while ((uint32_t)(HAL_GetTick() - start) < ms)
    {
        (void)GreenPakHost_ServiceWdtAutoToggle(HAL_GetTick());
    }
}

void TestContext_WaitUntilMs(uint32_t targetTickMs)
{
    while ((int32_t)(targetTickMs - HAL_GetTick()) > 0)
    {
        (void)GreenPakHost_ServiceWdtAutoToggle(HAL_GetTick());
    }
}

bool TestContext_CheckPin(TestPin_t pin, GPIO_PinState expected)
{
    return (TestContext_ReadPin(pin) == expected);
}

bool TestContext_ExpectPin(TestPin_t pin, GPIO_PinState expected, const char *detail)
{
    GPIO_PinState actual = TestContext_ReadPin(pin);
    bool ok = (actual == expected);

    printf("  %-24s %-4s  expected %-4s actual %-4s",
           TestContext_PinName(pin),
           ok ? "OK" : "FAIL",
           (expected == GPIO_PIN_SET) ? "HIGH" : "LOW",
           (actual == GPIO_PIN_SET) ? "HIGH" : "LOW");

    if (detail != NULL)
    {
        printf("  %s", detail);
    }

    printf("\r\n");
    return ok;
}
