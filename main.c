/**
blcan

@file		main.c
@author		Matej Kogovsek (matej@hamradio.si)
@copyright	LGPL 2.1
*/

#include "stm32f10x.h"

#include "can.h"

#include <string.h>

//-----------------------------------------------------------------------------
//  Defines
//-----------------------------------------------------------------------------

#define LED_PORT GPIOC
#define LED_PIN GPIO_Pin_13

#define TMR_ID_DELAY 0
#define TMR_ID_LED 1
#define TMR_ID_NUM 2

#define PAGE_SIZE 0x100

//-----------------------------------------------------------------------------
//  Typedefs
//-----------------------------------------------------------------------------

struct bl_pvars_t // size has to be a multiple of 4
{
	uint32_t app_page_count;
	uint32_t app_crc;
};

struct bl_cmd_t
{
	uint8_t brd;
	uint8_t cmd;
	uint16_t par1;
	uint32_t par2;
};

void bl_cmd_t_size_check(void)
{
	// if sizeof(bl_cmd_t) != 8, the compiler will throw a duplicate case value error
	switch(0) {case 0:case sizeof(struct bl_cmd_t) == 8:;}
}

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

static const uint16_t CANID_BOOTLOADER_CMD = 0xB0;
static const uint16_t CANID_BOOTLOADER_RPLY = 0xB1;

static const uint8_t BL_BOARD_ID = 1;

static const uint32_t MAGIC_VAL = (uint32_t)(0x36051bf3);
static uint32_t* const MAGIC_ADDR = (uint32_t*)(SRAM_BASE + 0x1000);

static const uint32_t* APP_BASE = (uint32_t*)(0x08002000);
static const uint16_t PAGE_COUNT = 64 - 8;

static const uint8_t BL_CMD_WRITE_BUF = 1;
static const uint8_t BL_CMD_WRITE_PAGE = 2;
static const uint8_t BL_CMD_WRITE_CRC = 3;

static const uint32_t NOCANRX_TO = 5;

//-----------------------------------------------------------------------------
//  Global variables
//-----------------------------------------------------------------------------

static volatile uint32_t uptime;
static volatile uint32_t lastcanrx;

static volatile uint32_t tmr_cnt[TMR_ID_NUM];
static uint32_t tmr_top[TMR_ID_NUM];

//-----------------------------------------------------------------------------
//  newlib required functions
//-----------------------------------------------------------------------------

void _exit(int status)
{
	while( 1 );
}

//int __errno; // required by math

//-----------------------------------------------------------------------------
//  Timers
//-----------------------------------------------------------------------------

void tmr_set(uint8_t tid, uint32_t cnt)
{
	tmr_top[tid] = cnt;
	tmr_cnt[tid] = cnt;
}

void tmr_reset(uint8_t tid)
{
	tmr_cnt[tid] = tmr_top[tid];
}

uint8_t tmr_elapsed(uint8_t tid)
{
	return (tmr_cnt[tid] == 0);
}

void tmr_tick(void)
{
	uint8_t i;
	for( i = 0; i < TMR_ID_NUM; ++i ) {
		if( tmr_cnt[i] > 0 )
			--(tmr_cnt[i]);
	}
}

//-----------------------------------------------------------------------------
//  SysTick handler
//-----------------------------------------------------------------------------

void SysTick_Handler(void)
{
	tmr_tick();

	static uint16_t mscnt = 0;
	if( ++mscnt == 1000 ) {
		mscnt = 0;
		++uptime;
	}
}

//-----------------------------------------------------------------------------
//  delays number of ms
//-----------------------------------------------------------------------------

void _delay_ms (uint32_t ms)
{
	tmr_set(TMR_ID_DELAY, ms);
	while( !tmr_elapsed(TMR_ID_DELAY));
}

//-----------------------------------------------------------------------------
//  utility functions
//-----------------------------------------------------------------------------

uint8_t __fls_wr(const uint32_t* page, const uint32_t* buf, uint32_t len)
{
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

	if( FLASH_COMPLETE != FLASH_ErasePage((uint32_t)page) ) {
		return 1;
	}

	uint32_t i;

	for( i = 0; i < len; ++i ) {
		if( FLASH_COMPLETE != FLASH_ProgramWord((uint32_t)page, *buf) ) {
			return 2;
		}
		++page;
		++buf;
	}

	return 0;
}

uint8_t fls_wr(const uint32_t* page, const uint32_t* buf, uint32_t len)
{
	// does flash equal buffer already?
	if( 0 == memcmp(page, buf, 4*len) ) {
		return 0;
	}

	FLASH_Unlock();
	uint8_t r = __fls_wr(page, buf, len);
	FLASH_Lock();
	if( r ) {
		return r;
	}

  // verify
	if( 0 != memcmp(page, buf, 4*len) ) {
		return 10;
	}

	return 0;
}

void DDR(GPIO_TypeDef* port, uint16_t pin, GPIOMode_TypeDef mode)
{
	if( port == GPIOA ) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE); }
	if( port == GPIOB ) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE); }
	if( port == GPIOC ) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE); }
	if( port == GPIOD ) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE); }
	if( port == GPIOE ) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE); }
	if( port == GPIOF ) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOF, ENABLE); }
	if( port == GPIOG ) { RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOG, ENABLE); }

	GPIO_InitTypeDef iotd;
	iotd.GPIO_Pin = pin;
	iotd.GPIO_Speed = GPIO_Speed_2MHz;
	iotd.GPIO_Mode = mode;
	GPIO_Init(port, &iotd);
}

void bl_tx_resp(uint8_t cmd, uint8_t ec)
{
	CanTxMsg m;
	m.IDE = CAN_Id_Standard;
	m.StdId = CANID_BOOTLOADER_RPLY;
	m.RTR = CAN_RTR_Data;
	m.DLC = 3;
	m.Data[0] = BL_BOARD_ID;
	m.Data[1] = cmd;
	m.Data[2] = ec;
	can_tx(&m);
}

void PreSystemInit(void)
{
	if( *(MAGIC_ADDR) == MAGIC_VAL ) {
		*(MAGIC_ADDR) = 0;
		__set_MSP(*(APP_BASE));
		uint32_t app = *(APP_BASE + 1); // +1 = 4 bytes since uint32_t
		asm("bx %0\n"::"r" (app):);
	}
}

//-----------------------------------------------------------------------------
//  CAN msg processing
//-----------------------------------------------------------------------------

void process_can_msg(CanRxMsg* msg)
{
	static uint32_t pagebuf[PAGE_SIZE];

	if( (msg->StdId == CANID_BOOTLOADER_CMD) && (msg->DLC == 8) ) {
		struct bl_cmd_t blc;
		memcpy(&blc, msg->Data, 8);
		if( blc.brd != BL_BOARD_ID ) return;
		lastcanrx = uptime;

		// write buffer command, par1 = offset, par2 =data
		if( blc.cmd == BL_CMD_WRITE_BUF ) {
			if( blc.par1 < PAGE_SIZE ) {
				pagebuf[blc.par1] = blc.par2;
				bl_tx_resp(blc.cmd, 0); // OK
			} else {
				bl_tx_resp(blc.cmd, 1); // invalid ofs
			}
			return;
		}

		// write page command, par1 = page number, par2 = crc
		if( blc.cmd == BL_CMD_WRITE_PAGE ) {
			if( blc.par1 < PAGE_COUNT) {
				CRC_ResetDR();
				CRC_CalcBlockCRC(pagebuf, PAGE_SIZE);
				if( CRC_GetCRC() == blc.par2 ) {
					uint32_t pgofs = blc.par1 * PAGE_SIZE;
					uint8_t r = fls_wr(APP_BASE + pgofs, pagebuf, PAGE_SIZE);
					if( r ) {
						bl_tx_resp(blc.cmd, 3); // verify failed
				  } else {
						bl_tx_resp(blc.cmd, 0); // OK
					}
				} else {
					bl_tx_resp(blc.cmd, 2); // invalid CRC
				}
			} else {
				bl_tx_resp(blc.cmd, 1); // invalid pagenum
			}
			return;
		}

		// write CRC command, par1 = number of pages, par2 = crc
		if( blc.cmd == BL_CMD_WRITE_CRC ) {
			if( blc.par1 <= PAGE_COUNT ) {
				CRC_ResetDR();
				CRC_CalcBlockCRC((uint32_t*)APP_BASE, blc.par1 * PAGE_SIZE);
				if( CRC_GetCRC() == blc.par2 ) {
					struct bl_pvars_t pv;
					pv.app_page_count = blc.par1;
					pv.app_crc = blc.par2;
					uint8_t r = fls_wr(APP_BASE - PAGE_SIZE, (uint32_t*)&pv, sizeof(pv)/4);
					if( r ) {
						bl_tx_resp(blc.cmd, 3); // verify failed
				  } else {
						bl_tx_resp(blc.cmd, 0); // OK
					}
				} else {
					bl_tx_resp(blc.cmd, 2); // invalid CRC
				}
			} else {
				bl_tx_resp(blc.cmd, 1); // invalid number of pages
			}
			return;
		}
	}
}

//-----------------------------------------------------------------------------
//  MAIN function
//-----------------------------------------------------------------------------

int main(void)
{
	if( SysTick_Config(SystemCoreClock / 1000) ) { // setup SysTick Timer for 1 msec interrupts
		while( 1 );                                  // capture error
	}

	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_CRC, ENABLE);

	IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
	IWDG_SetPrescaler(IWDG_Prescaler_32); // approx 3s
	IWDG_SetReload(0xfff);
	IWDG_ReloadCounter();
	IWDG_Enable();

	#ifdef LED_PIN
	DDR(LED_PORT, LED_PIN, GPIO_Mode_Out_PP);
	#endif

	can_init(CAN_BR_100);
	can_filter((uint32_t)CANID_BOOTLOADER_CMD << 21, (uint32_t)0x7ff << 21, 0);

	tmr_set(TMR_ID_LED, 100);

	while( 1 ) {
		// toggle LED (if defined)
		if( tmr_elapsed(TMR_ID_LED) ) {
			tmr_reset(TMR_ID_LED);
			#ifdef LED_PIN
			if( GPIO_ReadOutputDataBit(LED_PORT, LED_PIN) == Bit_SET ) {
				GPIO_ResetBits(LED_PORT, LED_PIN);
			} else {
				GPIO_SetBits(LED_PORT, LED_PIN);
			}
			#endif
		}

		// Process CAN messages
		CanRxMsg msg;
		if( can_rx(&msg) ) {
			process_can_msg(&msg);
		}

		// reset if no CAN messages received
		if( uptime - lastcanrx > NOCANRX_TO ) {
			struct bl_pvars_t pv;
			memcpy(&pv, APP_BASE - PAGE_SIZE, sizeof(pv));

			if( (pv.app_page_count > 0) && (pv.app_page_count <= PAGE_COUNT) ) {
				CRC_ResetDR();
				CRC_CalcBlockCRC((uint32_t*)APP_BASE, pv.app_page_count * PAGE_SIZE);
				if( CRC_GetCRC() == pv.app_crc ) {
					*(MAGIC_ADDR) = MAGIC_VAL;
				}
			}

			NVIC_SystemReset();
		}

		// feed watchdog
		IWDG_ReloadCounter();
	}
}
