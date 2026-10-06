/**********************************************************************
 * Zigbee + BLE dual mode RF slot switching (TLSR8258 concurrent mode).
 **********************************************************************/
#include "tl_common.h"
#include "zigbee_ble_switch.h"
#include "zb_common.h"
#include "device.h"
#include "app.h"
#include "stack/ble/ble.h"
#include "stack/ble/ble_config.h"
#include "stack/ble/ble_common.h"

app_dualModeInfo_t g_dualModeInfo = {
	.slot     = DUALMODE_SLOT_BLE,
	.bleState = BLS_LINK_STATE_IDLE,
};

extern u8 g_ble_txPowerSet;
extern u8 g_zb_txPowerSet;

_attribute_ram_code_ void switch_to_zb_context(void)
{
	ZB_RADIO_RX_DISABLE;

	backup_ble_rf_context();
	restore_zb_rf_context();

	/* switch tx power for zb mode */
	ZB_RADIO_TX_POWER_SET(g_zb_txPowerSet);

	ZB_RADIO_RX_ENABLE;

	CURRENT_SLOT_SET(DUALMODE_SLOT_ZIGBEE);
}

_attribute_ram_code_ void switch_to_ble_context(void)
{
	/* disable zb rx dma to avoid un-expected rx interrupt */
	ZB_RADIO_TX_DISABLE;
	ZB_RADIO_RX_DISABLE;

	restore_ble_rf_context();

	/* switch tx power for ble mode */
	ZB_RADIO_TX_POWER_SET(g_ble_txPowerSet);

	ZB_RADIO_RX_ENABLE;

	CURRENT_SLOT_SET(DUALMODE_SLOT_BLE);
}

inline int is_switch_to_ble(void)
{
	return get_ble_next_event_tick() - (reg_system_tick + ZIGBEE_AFTER_TIME) > BIT(31);
}

inline int is_switch_to_zigbee(void)
{
	return get_ble_next_event_tick() - (reg_system_tick + BLE_IDLE_TIME) < BIT(31);
}

void zb_task(void)
{
	ev_main();

#if (MODULE_WATCHDOG_ENABLE)
	drv_wd_clear();
#endif

	tl_zbTaskProcedure();
}

void concurrent_mode_main_loop(void)
{
	u32 r = 0;

	APP_BLE_STATE_SET(BLE_BLT_STATE_GET());

	if(CURRENT_SLOT_GET() == DUALMODE_SLOT_BLE){
		/* ble task */
		blt_sdk_main_loop();

		r = drv_disable_irq();

		if(((get_ble_event_state() && is_switch_to_zigbee()) || APP_BLE_STATE_GET() == BLS_LINK_STATE_IDLE)
			&& (!g_dualModeInfo.switch_to_ble) && !g_appCtx.bleOnly){
			/* ready to switch to ZIGBEE mode */
			switch_to_zb_context();

			drv_restore_irq(r);
			zb_task();
		}else{
			drv_restore_irq(r);
		}
	}else{
		/* now in zigbee mode */
		r = drv_disable_irq();

		if((( !zb_rfTxDoing() && is_switch_to_ble() && APP_BLE_STATE_GET() != BLS_LINK_STATE_IDLE)
			|| g_dualModeInfo.switch_to_ble) && !g_appCtx.bleOnly){
			/* ready to switch to BLE mode */
			switch_to_ble_context();

			drv_restore_irq(r);
			return;
		}

		drv_restore_irq(r);
		zb_task();
	}
}
