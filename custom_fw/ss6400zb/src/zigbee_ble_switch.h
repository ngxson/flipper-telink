#ifndef _ZIGBEE_BLE_SWITCH_H_
#define _ZIGBEE_BLE_SWITCH_H_

/* The SDK's mac_phy.c (built with BLE_CONCURRENT_MODE) asks which stack
 * owns the radio. This firmware never runs both at once: service mode is
 * BLE only and never starts the Zigbee MAC, Zigbee mode never starts BLE,
 * so the slot is simply fixed to Zigbee (zb_dev.c). */

typedef enum{
	DUALMODE_SLOT_BLE = 0,
	DUALMODE_SLOT_ZIGBEE,
}app_currentSlot_e;

typedef struct{
	volatile app_currentSlot_e slot;
	u8       bleState;
	u8		 switch_to_ble;
}app_dualModeInfo_t;

extern app_dualModeInfo_t g_dualModeInfo;

#define CURRENT_SLOT_GET()			 g_dualModeInfo.slot
#define CURRENT_SLOT_SET(s)			 g_dualModeInfo.slot = s

#define ZB_RF_ISR_RECOVERY		do{  \
									if(CURRENT_SLOT_GET() == DUALMODE_SLOT_ZIGBEE) rf_set_irq_mask(FLD_RF_IRQ_RX|FLD_RF_IRQ_TX);  \
								}while(0)

#endif /* _ZIGBEE_BLE_SWITCH_H_ */
