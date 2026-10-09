/*
 * Interrupt handler. The two modes never run together, so this is a plain
 * dispatch (the SDK's irq_handler.c is the Zigbee/BLE concurrent one and
 * is not linked):
 *   service mode: everything to the BLE controller, plus the fingerprint UART
 *   Zigbee mode:  RF + hw timers to the Zigbee stack (zb_dev.c)
 */
#include "tl_common.h"
#include "fp.h"
#include "zb_dev.h"

extern void irq_blt_sdk_handler(void);
extern u8 g_serviceMode;

_attribute_ram_code_ void irq_handler(void)
{
	if(!g_serviceMode){
		zb_irqHandler();
		return;
	}

	irq_blt_sdk_handler();

	/* fingerprint module UART (non-DMA RX, one irq per byte) */
	if(reg_irq_mask & FLD_IRQ_UART_EN){
		if(uart_ndmairq_get()){
			fp_uartIrq();
		}
	}
}
