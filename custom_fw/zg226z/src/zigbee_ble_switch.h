#ifndef _ZIGBEE_BLE_SWITCH_H_
#define _ZIGBEE_BLE_SWITCH_H_

#define  ZIGBEE_AFTER_TIME    (16 * 1000 * 4)	/* 5 ms */
#define  BLE_IDLE_TIME   	  (16 * 1000 * 4)	/* 5 ms */

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
#define APP_BLE_STATE_SET(state)	 g_dualModeInfo.bleState = state
#define APP_BLE_STATE_GET()			 g_dualModeInfo.bleState

#define ZB_RF_ISR_RECOVERY		do{  \
									if(CURRENT_SLOT_GET() == DUALMODE_SLOT_ZIGBEE) rf_set_irq_mask(FLD_RF_IRQ_RX|FLD_RF_IRQ_TX);  \
								}while(0)

_attribute_ram_code_ void switch_to_zb_context(void);
_attribute_ram_code_ void switch_to_ble_context(void);

int is_switch_to_ble(void);
int is_switch_to_zigbee(void);

void zb_task(void);
void concurrent_mode_main_loop(void);

#endif /* _ZIGBEE_BLE_SWITCH_H_ */
