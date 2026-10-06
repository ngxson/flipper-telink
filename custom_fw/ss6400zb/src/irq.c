/*
 * BLE-only interrupt handler. The SDK's irq_handler.c dispatches between
 * the Zigbee and BLE stacks (concurrent mode); with no Zigbee stack
 * everything goes to the BLE controller, plus the fingerprint UART.
 */
#include "tl_common.h"
#include "fp.h"

extern void irq_blt_sdk_handler(void);

_attribute_ram_code_ void irq_handler(void)
{
	irq_blt_sdk_handler();

	/* fingerprint module UART (non-DMA RX, one irq per byte) */
	if(reg_irq_mask & FLD_IRQ_UART_EN){
		if(uart_ndmairq_get()){
			fp_uartIrq();
		}
	}
}
