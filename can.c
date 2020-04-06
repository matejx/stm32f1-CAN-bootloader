/**
@file		can.c
@brief		CAN bus routines
@author		Matej Kogovsek (matej@hamradio.si)
@copyright	LGPL 2.1
@note		This file is part of mat-stm32f1-lib
*/

#include <stm32f10x.h>
#include <stm32f10x_can.h>

void can_init(uint16_t br)
{
	GPIO_InitTypeDef iotd;

	// configure CAN1 IOs
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO | RCC_APB2Periph_GPIOB, ENABLE);

	// configure CAN1 RX pin
	iotd.GPIO_Pin = GPIO_Pin_8;
	iotd.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_Init(GPIOB, &iotd);

	// configure CAN1 TX pin
	iotd.GPIO_Pin = GPIO_Pin_9;
	iotd.GPIO_Mode = GPIO_Mode_AF_PP;
	iotd.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &iotd);

	// remap CAN1 GPIOs to GPIOB
	GPIO_PinRemapConfig(GPIO_Remap1_CAN1, ENABLE);

	// CAN1 periph clocks enable
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);

	// CAN1 register init
	CAN_DeInit(CAN1);

	// init CAN1
	CAN_InitTypeDef cnis;
	CAN_StructInit(&cnis);
	cnis.CAN_TTCM = DISABLE;
	cnis.CAN_ABOM = DISABLE;
	cnis.CAN_AWUM = DISABLE;
	cnis.CAN_NART = ENABLE;
	cnis.CAN_RFLM = DISABLE;
	cnis.CAN_TXFP = DISABLE;
	cnis.CAN_Mode = CAN_Mode_Normal;
	cnis.CAN_SJW = CAN_SJW_1tq;
	cnis.CAN_BS1 = CAN_BS1_3tq;
	cnis.CAN_BS2 = CAN_BS2_2tq;
	cnis.CAN_Prescaler = br;
	CAN_Init(CAN1, &cnis);
}

uint8_t can_filter(uint32_t id, uint32_t msk, uint8_t canfilnum)
{
	if( canfilnum >= 14 ) return 0;

	// CAN1 filter init
	CAN_FilterInitTypeDef fitd;
	fitd.CAN_FilterNumber = canfilnum;
	fitd.CAN_FilterMode = CAN_FilterMode_IdMask;
	fitd.CAN_FilterScale = CAN_FilterScale_32bit;
	fitd.CAN_FilterIdHigh = id >> 16;
	fitd.CAN_FilterIdLow = 0;
	fitd.CAN_FilterMaskIdHigh = msk >> 16;
	fitd.CAN_FilterMaskIdLow = 0;
	fitd.CAN_FilterFIFOAssignment = CAN_Filter_FIFO0;
	fitd.CAN_FilterActivation = ENABLE;
	CAN_FilterInit(&fitd);

	return canfilnum;
}

uint8_t can_tx(CanTxMsg* msg)
{
	return CAN_TxStatus_NoMailBox != CAN_Transmit(CAN1, msg);
}

uint8_t can_rx(CanRxMsg* msg)
{
	if( CAN_MessagePending(CAN1, CAN_FIFO0) ) {
		CAN_Receive(CAN1, CAN_FIFO0, msg);
		return 1;
	}
	return 0;
}
