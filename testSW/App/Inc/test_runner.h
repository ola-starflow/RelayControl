#ifndef TEST_RUNNER_H
#define TEST_RUNNER_H

#include <stdint.h>

void TestRunner_PrintMenu(const char *message);
void TestRunner_PrintList(void);
void TestRunner_ApplyDefaultCommand(void);
void TestRunner_RunAll(void);
void TestRunner_RunById(uint16_t id);

#endif /* TEST_RUNNER_H */
