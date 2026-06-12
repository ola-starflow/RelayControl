/*
 * sSerial.c
 *
 *  Created on: Dec 30, 2024
 *      Author: olast
 */


#include "sSerial.h"




bool unbufferedTx(SSerial_HandleTypeDef *sSerial, uint8_t *data, uint16_t size);

bool setupDMA(SSerial_HandleTypeDef * sSerial);

bool sSerial_txReady(SSerial_HandleTypeDef *sSerial)
{
	#ifdef STM32H563xx
	return (((sSerial->txDmaChannel->CBR1 & DMA_CBR1_BNDT) == 0) && ((sSerial->uartBase->ISR & USART_ISR_TXFE) == USART_ISR_TXFE)); //TODO check if this is safe
	#endif

	#ifdef STM32G474xx
	return (sSerial->txDmaChannel->CNDTR == 0);
	#endif
}

void sSerial_enableRx(SSerial_HandleTypeDef *sSerial, bool enable)
{
	if(enable)
	{
		ATOMIC_SET_BIT(sSerial->uartBase->CR1, USART_CR1_RE);
	}
	else
	{
		ATOMIC_CLEAR_BIT(sSerial->uartBase->CR1, USART_CR1_RE);
	}
}




bool sSerial_init(SSerial_HandleTypeDef * sSerial)
{
	sSerial->RxBuffReadPos = 0;
	sSerial->TxBuffReadPos = 0;
	sSerial->TxBuffDmaEndPos = 0;
	sSerial->TxBuffWritePos = 0;	

	if(!setupDMA(sSerial)){ return false; }

	sSerial_enableRx(sSerial, true);

	return true;
}


void sSerial_update(SSerial_HandleTypeDef *sSerial)
{
	//verify that we have buffered TX
	if((sSerial->TxBufferSize == 0) || (sSerial->pTxBuffPtr == NULL)) {	return;	}
	//check if TX buffer is empty
	if(sSerial->TxBuffWritePos == sSerial->TxBuffReadPos) { return; }
	//check if DMA transfer is ongoing
	if(!sSerial_txReady(sSerial)) { return; }
	//DMA transfer complete, check if we should update read pointer
	if(sSerial->TxBuffReadPos != sSerial->TxBuffDmaEndPos)
	{
		sSerial->TxBuffReadPos = sSerial->TxBuffDmaEndPos;
		//check if TX buffer is empty
		if(sSerial->TxBuffWritePos == sSerial->TxBuffReadPos) { return; }
	}
	//at this point the DMA is ready, and there is data in buffer. Start new DMA transfer
	if(sSerial->TxBuffWritePos < sSerial->TxBuffReadPos)
	{//send data until end of ring buffer
		if(unbufferedTx(sSerial, &sSerial->pTxBuffPtr[sSerial->TxBuffReadPos], sSerial->TxBufferSize - sSerial->TxBuffReadPos))
		{
			sSerial->TxBuffDmaEndPos = 0;
		}
	}
	else
	{//send remaining data in ring buffer
		if(unbufferedTx(sSerial, &sSerial->pTxBuffPtr[sSerial->TxBuffReadPos], sSerial->TxBuffWritePos - sSerial->TxBuffReadPos))
		{
			sSerial->TxBuffDmaEndPos = sSerial->TxBuffWritePos;
		}
	}
}


bool bufferedTx(SSerial_HandleTypeDef *sSerial, uint8_t *data, uint16_t size)
{
	uint16_t spaceInBuffer = 0;

	//check if DMA transfer is complete, but readPtr is not updated.
	if((sSerial->TxBuffReadPos != sSerial->TxBuffDmaEndPos) && (sSerial_txReady(sSerial)))
	{
		sSerial->TxBuffReadPos = sSerial->TxBuffDmaEndPos;
	}
	//find available space in buffer
	if(sSerial->TxBuffReadPos > sSerial->TxBuffWritePos)
	{
		spaceInBuffer = sSerial->TxBuffReadPos - sSerial->TxBuffWritePos - 1;
	}
	else
	{
		spaceInBuffer = sSerial->TxBufferSize - sSerial->TxBuffWritePos + sSerial->TxBuffReadPos - 1;
	}
	//if room, copy to tx buffer
	if(spaceInBuffer < size) { return false; }
	while(size > 0)
	{
		sSerial->pTxBuffPtr[sSerial->TxBuffWritePos] = *data;
		sSerial->TxBuffWritePos++;
		if(sSerial->TxBuffWritePos >= sSerial->TxBufferSize) { sSerial->TxBuffWritePos = 0; }
		data++;
		size--;
	}
	sSerial_update(sSerial);
	return true;
}



bool sSerial_tx(SSerial_HandleTypeDef *sSerial, uint8_t *data, uint16_t size)
{
	if((sSerial->TxBufferSize == 0) || (sSerial->pTxBuffPtr == NULL))
	{
		return unbufferedTx(sSerial, data, size);
	}
	else
	{
		return bufferedTx(sSerial, data, size);
	}
}


bool sSerial_rx(SSerial_HandleTypeDef *sSerial, uint8_t *rxData)
{
	#ifdef STM32H563xx
	if((sSerial->RxBufferSize - sSerial->RxBuffReadPos) != (uint16_t) (sSerial->rxDmaChannel->CBR1 & 0xFFFF))
	#endif

	#ifdef STM32G474xx
	if((sSerial->RxBufferSize - sSerial->RxBuffReadPos) != (uint16_t) (sSerial->rxDmaChannel->CNDTR & 0xFFFF))
	#endif	
	{
		*rxData = sSerial->pRxBuffPtr[sSerial->RxBuffReadPos];
		sSerial->RxBuffReadPos = sSerial->RxBuffReadPos + 1;
		if(sSerial->RxBuffReadPos >= sSerial->RxBufferSize) { sSerial->RxBuffReadPos = 0; }
		return true;
	}
	return false;
}


bool sSerial_rxBlocking(SSerial_HandleTypeDef *sSerial, uint8_t* rxData, uint16_t length, uint32_t timeoutMs)
{
	uint32_t startTick = HAL_GetTick();
	uint16_t bytesReceived = 0;
	while(((HAL_GetTick() - startTick) < timeoutMs) && (bytesReceived < length))
	{
		if(sSerial_rx(sSerial, &rxData[bytesReceived]))
		{
			bytesReceived++;
		}
		HAL_Delay(1);
	}
	return (bytesReceived == length);
}

void sSerial_flushRx(SSerial_HandleTypeDef *sSerial)
{
#ifdef STM32H563xx
	sSerial->RxBuffReadPos = sSerial->RxBufferSize - (sSerial->rxDmaChannel->CBR1);
#endif

#ifdef STM32G474xx
	sSerial->RxBuffReadPos = sSerial->RxBufferSize - (sSerial->rxDmaChannel->CNDTR);
#endif		
}





bool unbufferedTx(SSerial_HandleTypeDef *sSerial, uint8_t *data, uint16_t size)
{
	if(!sSerial_txReady(sSerial)) { return false; }

#ifdef STM32H563xx
	//Disable the specified DMA Channel.
	SET_BIT(sSerial->txDmaChannel->CCR, (DMA_CCR_SUSP | DMA_CCR_RESET));
	/* Configure the DMA channel data size */
	MODIFY_REG(sSerial->txDmaChannel->CBR1, DMA_CBR1_BNDT, (size & DMA_CBR1_BNDT));
	 /* Configure DMA channel source address */
	WRITE_REG(sSerial->txDmaChannel->CSAR, (uint32_t)data);
	/* Enable DMA channel */
	SET_BIT(sSerial->txDmaChannel->CCR, DMA_CCR_EN);
#endif

#ifdef STM32G474xx
	//Disable the specified DMA Channel.
	CLEAR_BIT(sSerial->txDmaChannel->CCR, DMA_CCR_EN);
	/* Configure the DMA channel data size */
	WRITE_REG(sSerial->txDmaChannel->CNDTR, size);
	 /* Configure DMA channel source address */
	WRITE_REG(sSerial->txDmaChannel->CMAR, (uint32_t)data);
	/* Enable DMA channel */
	SET_BIT(sSerial->txDmaChannel->CCR, DMA_CCR_EN);
#endif

    /* Enable the DMA transfer for transmit request by setting the DMAT bit in the UART CR3 register */
	ATOMIC_SET_BIT(sSerial->uartBase->CR3, USART_CR3_DMAT);// Enable the DMA transfer for transmit request by setting the DMAT bit	in the UART CR3 registerr

	return true;
}


bool setupDMA(SSerial_HandleTypeDef * sSerial)
{

	
    uint32_t rxReq;
    uint32_t txReq;

#ifdef STM32H563xx
	uint32_t tmpReg;
    if(     sSerial->uartBase == USART1)  {txReq = LL_GPDMA1_REQUEST_USART1_TX;  rxReq = LL_GPDMA1_REQUEST_USART1_RX;  }
    else if(sSerial->uartBase == USART2)  {txReq = LL_GPDMA1_REQUEST_USART2_TX;  rxReq = LL_GPDMA1_REQUEST_USART2_RX;  }
    else if(sSerial->uartBase == USART3)  {txReq = LL_GPDMA1_REQUEST_USART3_TX;  rxReq = LL_GPDMA1_REQUEST_USART3_RX;  }
    else if(sSerial->uartBase == USART6)  {txReq = LL_GPDMA1_REQUEST_USART6_TX;  rxReq = LL_GPDMA1_REQUEST_USART6_RX;  }
    else if(sSerial->uartBase == USART10) {txReq = LL_GPDMA1_REQUEST_USART10_TX; rxReq = LL_GPDMA1_REQUEST_USART10_RX; }
    else if(sSerial->uartBase == USART11) {txReq = LL_GPDMA1_REQUEST_USART11_TX; rxReq = LL_GPDMA1_REQUEST_USART11_RX; }
    else {return false; } //for now only USART are testet and used, if UART is needed, it needs to verifed to work the same way


	//Enable the AHB1 peripheral clock.
	SET_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPDMA1EN);
	/* Delay after an RCC peripheral clock enabling */ \
	tmpReg = READ_BIT(RCC->AHB1ENR, RCC_AHB1ENR_GPDMA1EN); \
	(void) (tmpReg);

    

	//Setup DMA for TX

	/* Write DMA Channel Control Register (CCR) */
	WRITE_REG(sSerial->txDmaChannel->CCR, 0);

	/* Write DMA Channel Transfer Register 1 (CTR1) */
	WRITE_REG(sSerial->txDmaChannel->CTR1, (LL_DMA_SRC_INCREMENT | LL_DMA_DEST_FIXED | LL_DMA_SRC_DATAWIDTH_BYTE |	LL_DMA_DEST_DATAWIDTH_BYTE | LL_DMA_SRC_ALLOCATED_PORT1 | LL_DMA_DEST_ALLOCATED_PORT0));

	/* Write DMA Channel Transfer Register 2 (CTR2) */
	WRITE_REG(sSerial->txDmaChannel->CTR2, (LL_DMA_HWREQUEST_SINGLEBURST | txReq | LL_DMA_TCEM_BLK_TRANSFER | LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_NORMAL));

	/* Write DMA Channel Block Register 1 (CBR1) */
	WRITE_REG(sSerial->txDmaChannel->CBR1, 0U);

	/* Write DMA Channel linked-list address register (CLLR) */
	WRITE_REG(sSerial->txDmaChannel->CLLR, 0U);

	/* Configure DMA channel destination address */
	sSerial->txDmaChannel->CDAR = (uint32_t)&sSerial->uartBase->TDR;



	//Setup DMA for RX
	sSerial->dmaLinkedListDesc[0] = (uint32_t) sSerial->pRxBuffPtr;

	/* Write DMA Channel Control Register (CCR) */
	WRITE_REG(sSerial->rxDmaChannel->CCR, 0);

	/* Write DMA Channel Transfer Register 1 (CTR1) */
	WRITE_REG(sSerial->rxDmaChannel->CTR1, LL_DMA_SRC_FIXED | LL_DMA_DEST_INCREMENT | LL_DMA_SRC_DATAWIDTH_BYTE | LL_DMA_DEST_DATAWIDTH_BYTE | LL_DMA_SRC_ALLOCATED_PORT0 | LL_DMA_DEST_ALLOCATED_PORT1);

	/* Write DMA Channel Transfer Register 2 (CTR2) */
	WRITE_REG(sSerial->rxDmaChannel->CTR2, LL_DMA_HWREQUEST_SINGLEBURST | rxReq | LL_DMA_TCEM_BLK_TRANSFER |	LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_NORMAL);

	/* Write DMA Channel Block Register 1 (CBR1) */
	WRITE_REG(sSerial->rxDmaChannel->CBR1, sSerial->RxBufferSize);

	/* Configure DMA channel source address */
	WRITE_REG(sSerial->rxDmaChannel->CSAR, (uint32_t)&sSerial->uartBase->RDR);

	/* Configure DMA channel destination address */
	WRITE_REG(sSerial->rxDmaChannel->CDAR, (uint32_t)sSerial->pRxBuffPtr);

	/* Write DMA Channel linked-list address register (CLLR) */
	WRITE_REG(sSerial->rxDmaChannel->CLLR, ((uint32_t)sSerial->dmaLinkedListDesc & 0x0000FFFF) | DMA_CLLR_UDA);

	/* Write DMA Channel linked-list base address register */
	WRITE_REG(sSerial->rxDmaChannel->CLBAR, (uint32_t)sSerial->dmaLinkedListDesc & 0xFFFF0000);
	
#endif //#ifdef STM32H563xx

#ifdef STM32G474xx
	DMAMUX_Channel_TypeDef * dmaMuxRx;
    DMAMUX_Channel_TypeDef * dmaMuxTx;

	if(     sSerial->uartBase == USART1)  {txReq = LL_DMAMUX_REQ_USART1_TX;  rxReq = LL_DMAMUX_REQ_USART1_RX;  }
    else if(sSerial->uartBase == USART2)  {txReq = LL_DMAMUX_REQ_USART2_TX;  rxReq = LL_DMAMUX_REQ_USART2_RX;  }
    else if(sSerial->uartBase == USART3)  {txReq = LL_DMAMUX_REQ_USART3_TX;  rxReq = LL_DMAMUX_REQ_USART3_RX;  }
    else if(sSerial->uartBase == LPUART1)  {txReq = LL_DMAMUX_REQ_LPUART1_TX;  rxReq = LL_DMAMUX_REQ_LPUART1_RX;  }
    else if(sSerial->uartBase == UART4) {txReq = LL_DMAMUX_REQ_UART4_TX; rxReq = LL_DMAMUX_REQ_UART4_RX; }
    else if(sSerial->uartBase == UART5) {txReq = LL_DMAMUX_REQ_UART5_TX; rxReq = LL_DMAMUX_REQ_UART5_RX; }
    else {return false; } //Not supported

	if	   (sSerial->txDmaChannel == DMA1_Channel1) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel0_BASE; }
	else if(sSerial->txDmaChannel == DMA1_Channel2) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel1_BASE; }
	else if(sSerial->txDmaChannel == DMA1_Channel3) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel2_BASE; }
	else if(sSerial->txDmaChannel == DMA1_Channel4) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel3_BASE; }
	else if(sSerial->txDmaChannel == DMA1_Channel5) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel4_BASE; }
	else if(sSerial->txDmaChannel == DMA1_Channel6) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel5_BASE; }
	else if(sSerial->txDmaChannel == DMA1_Channel7) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel6_BASE; }
	else if(sSerial->txDmaChannel == DMA1_Channel8) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel7_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel1) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel8_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel2) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel9_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel3) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel10_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel4) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel11_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel5) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel12_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel6) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel13_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel7) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel14_BASE; }
	else if(sSerial->txDmaChannel == DMA2_Channel8) {dmaMuxTx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel15_BASE; }
	else { return false; } //Not correctly set up


	if	   (sSerial->rxDmaChannel == DMA1_Channel1) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel0_BASE; }
	else if(sSerial->rxDmaChannel == DMA1_Channel2) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel1_BASE; }
	else if(sSerial->rxDmaChannel == DMA1_Channel3) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel2_BASE; }
	else if(sSerial->rxDmaChannel == DMA1_Channel4) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel3_BASE; }
	else if(sSerial->rxDmaChannel == DMA1_Channel5) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel4_BASE; }
	else if(sSerial->rxDmaChannel == DMA1_Channel6) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel5_BASE; }
	else if(sSerial->rxDmaChannel == DMA1_Channel7) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel6_BASE; }
	else if(sSerial->rxDmaChannel == DMA1_Channel8) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel7_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel1) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel8_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel2) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel9_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel3) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel10_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel4) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel11_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel5) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel12_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel6) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel13_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel7) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel14_BASE; }
	else if(sSerial->rxDmaChannel == DMA2_Channel8) {dmaMuxRx = (DMAMUX_Channel_TypeDef *) DMAMUX1_Channel15_BASE; }
	else { return false; } //Not correctly set up

	/* DMA controller clock enable */
  	LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMAMUX1);
	if(sSerial->dmaNumber == DMA1)
	{
		LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);
	} 
	else
	{
		LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA2);

	}
	

	// DMA_RX Init 
	WRITE_REG(sSerial->rxDmaChannel->CCR, 0);// Disable the dma

	//-------------------------- DMAx CMAR Configuration -------------------------
	sSerial->rxDmaChannel->CMAR = (uint32_t) sSerial->pRxBuffPtr; 

	//-------------------------- DMAx CPAR Configuration -------------------------
	sSerial->rxDmaChannel->CPAR = (uint32_t) &sSerial->uartBase->RDR; 

	//--------------------------- DMAx CNDTR Configuration -----------------------
	sSerial->rxDmaChannel->CNDTR = sSerial->RxBufferSize;// Configure DMA Channel data length

	//-------------------------- DMAx CCR Configuration -------------------------
	WRITE_REG(sSerial->rxDmaChannel->CCR,  (LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_PRIORITY_LOW | 
	 										LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT | 
											LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_BYTE | 
											LL_DMA_MDATAALIGN_BYTE));

	//-------------------------- DMAMUX CCR Configuration -------------------------
	WRITE_REG(dmaMuxRx->CCR, rxReq);



	// DMA_TX Init 
	WRITE_REG(sSerial->txDmaChannel->CCR, 0);// Disable the dma

	//-------------------------- DMAx CPAR Configuration -------------------------
	WRITE_REG(sSerial->txDmaChannel->CPAR, (uint32_t)&sSerial->uartBase->TDR);// Configure DMA Channel destination address

	WRITE_REG(sSerial->txDmaChannel->CCR,  (LL_DMA_DIRECTION_MEMORY_TO_PERIPH | LL_DMA_PRIORITY_LOW | 
	 										LL_DMA_MODE_NORMAL | LL_DMA_PERIPH_NOINCREMENT | 
											LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_BYTE | 
											LL_DMA_MDATAALIGN_BYTE));

	//-------------------------- DMAMUX CCR Configuration -------------------------
	WRITE_REG(dmaMuxTx->CCR, txReq);	
#endif //#ifdef STM32G474xx


	ATOMIC_SET_BIT(sSerial->uartBase->CR3, USART_CR3_DMAR); //Enable the DMA transfer for the receiver request by setting the DMAR bit in the UART CR3 register
	ATOMIC_SET_BIT(sSerial->rxDmaChannel->CCR, DMA_CCR_EN); // Enable DMA channel

//
 return true;
}
/*
void setupUSART(SSerial_HandleTypeDef * sSerial)
{
	// Disable UART
	sSerial->uartBase->CR1 = 0;

	// Set the UART Communication parameters
	WRITE_REG(sSerial->uartBase->CR1,(USART_CR1_TE | USART_CR1_RE | USART_CR1_FIFOEN));
	WRITE_REG(sSerial->uartBase->CR2, 0);
	WRITE_REG(sSerial->uartBase->CR3, 0);

	//TODO figure out baudrate
	sSerial->uartBase->BRR = 1085; //115200 @ 125MHz

	// Enable UART
	sSerial->uartBase->CR1 |= USART_CR1_UE;
}*/
