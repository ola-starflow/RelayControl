#include "debug_console.h"
#include "main.h"
#include "sSerial.h"

#include <stddef.h>
#include <stdint.h>

static uint8_t debugRxBuffer[128];
static uint8_t debugTxBuffer[4096];
static SSerial_HandleTypeDef debugSerialHandle __attribute__((aligned(8)));

void DebugConsole_Init(void)
{
    debugSerialHandle.uartBase = LPUART1;
    debugSerialHandle.baudrate = 115200;
    debugSerialHandle.pRxBuffPtr = debugRxBuffer;
    debugSerialHandle.pTxBuffPtr = debugTxBuffer;
    debugSerialHandle.RxBufferSize = sizeof(debugRxBuffer);
    debugSerialHandle.TxBufferSize = sizeof(debugTxBuffer);
    debugSerialHandle.dmaNumber = DMA1;
    debugSerialHandle.txDmaChannel = DMA1_Channel2;
    debugSerialHandle.rxDmaChannel = DMA1_Channel1;

    sSerial_init(&debugSerialHandle);
}

void DebugConsole_Update(void)
{
    sSerial_update(&debugSerialHandle);
}

bool DebugConsole_ReadChar(uint8_t *ch)
{
    if (ch == NULL)
    {
        return false;
    }

    return sSerial_rx(&debugSerialHandle, ch);
}

void DebugConsole_FlushMs(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++)
    {
        HAL_Delay(1);
        DebugConsole_Update();
    }
}

int _write(int file, char *ptr, int len)
{
    (void)file;

    if ((ptr == NULL) || (len <= 0))
    {
        return 0;
    }

    sSerial_tx(&debugSerialHandle, (uint8_t *)ptr, (uint16_t)len);
    return len;
}
