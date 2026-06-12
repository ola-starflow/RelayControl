/*
 * sSerial.h
 *
 *  Created on: Dec 30, 2024
 *      Author: ola
 */

#ifndef INC_SSERIAL_H_
#define INC_SSERIAL_H_
#include "stdbool.h"
#ifdef STM32H563xx
#include "stm32h5xx.h"
#include "stm32h5xx_ll_dma.h"
#endif

#ifdef STM32G474xx
#include "stm32g4xx.h"
#include "stm32g4xx_ll_dma.h"
#include "stm32g4xx_ll_bus.h"
#endif



//must be aligned on creation using  __attribute__((aligned(8)))
//example: SSerial_HandleTypeDef debugSerialHandle __attribute__((aligned(8)));
typedef struct __SSerial_HandleTypeDef
{
	uint32_t 			     dmaLinkedListDesc[8];  /* internal variable, is setup automatically in sSerial_init() */

	DMA_Channel_TypeDef      *txDmaChannel;			/* TX DMA channel to be used */
	DMA_Channel_TypeDef      *rxDmaChannel;			/* RX DMA channel to be used */
	DMA_TypeDef              *dmaNumber;			/* DMA to be used (only used on STM32G4) */
	USART_TypeDef            *uartBase;             /* UART to use */
	uint32_t                 baudrate;				/* UART baud rate */
	uint8_t                  *pRxBuffPtr;           /* pointer to RX buffer to use */
	uint8_t                  *pTxBuffPtr;           /* pointer to TX buffer to use */
	uint16_t                 RxBufferSize;          /* size of RX buffer to use */
	uint16_t                 TxBufferSize;          /* size of TX buffer to use */

	uint16_t				 RxBuffReadPos;			/* internal variable, is setup automatically in sSerial_init() */
	uint16_t				 TxBuffReadPos;			/* internal variable, is setup automatically in sSerial_init() */
	uint16_t				 TxBuffDmaEndPos;		/* internal variable, is setup automatically in sSerial_init() */
	uint16_t				 TxBuffWritePos;		/* internal variable, is setup automatically in sSerial_init() */


} SSerial_HandleTypeDef;


/** @brief  Init a new serial port. USART and DMA is configured here.
  * However the GPIO configuration is the responsibility of the caller.  *
  *
  */
bool sSerial_init(SSerial_HandleTypeDef * sSerial);

void sSerial_enableRx(SSerial_HandleTypeDef *sSerial, bool enable);
bool sSerial_rx(SSerial_HandleTypeDef *sSerial, uint8_t *rxData);
bool sSerial_rxBlocking(SSerial_HandleTypeDef *sSerial, uint8_t* rxData, uint16_t length, uint32_t timeoutMs);
void sSerial_flushRx(SSerial_HandleTypeDef *sSerial);

bool sSerial_txReady(SSerial_HandleTypeDef *sSerial);
bool sSerial_tx(SSerial_HandleTypeDef *sSerial, uint8_t *data, uint16_t size);

void sSerial_update(SSerial_HandleTypeDef *sSerial);  //must be called periodically if using buffered TX



#endif /* INC_SSERIAL_H_ */
