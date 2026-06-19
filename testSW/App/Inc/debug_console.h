#ifndef DEBUG_CONSOLE_H
#define DEBUG_CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

void DebugConsole_Init(void);
void DebugConsole_Update(void);
bool DebugConsole_ReadChar(uint8_t *ch);
void DebugConsole_FlushMs(uint32_t ms);

#endif /* DEBUG_CONSOLE_H */
