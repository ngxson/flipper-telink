/**********************************************************************
 * Low power management for the ZG-228Z.
 *
 * Normal (Zigbee) mode: BLE stays idle, so whenever the Zigbee stack is
 * idle the system enters deep sleep with retention and wakes on the
 * vibration pad, the button, or the TX pad (BLE mode bridge).
 * BLE-only mode: never sleep, so the LED blink and GATT stay alive.
 **********************************************************************/
#include "tl_common.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "device.h"
#include "app.h"
#include "app_cfg.h"
#include "app_ui.h"
#include "zigbee_ble_switch.h"
#include "stack/ble/ble_config.h"
#include "stack/ble/ble_common.h"
#include "stack/ble/ble.h"

#if PM_ENABLE

bool app_zigbeeIdle(void)
{
	bool ret = bdb_isIdle() && !tl_stackBusy() && zb_isTaskDone() && !ev_timer_process(true);

	/* keep the CPU awake while the alarm or a melody is playing */
	if(ret && app_isBusy()){
		ret = FALSE;
	}

	return ret;
}

void app_pm_task(void)
{
	if(g_appCtx.bleOnly){
		/* BLE mode: LED blink + GATT, stay awake */
		return;
	}

	if(APP_BLE_STATE_GET() == BLS_LINK_STATE_CONN || APP_BLE_STATE_GET() == BLS_LINK_STATE_ADV){
		/* BLE is active in this build only in BLE-only mode; defensive */
		return;
	}

	g_dualModeInfo.switch_to_ble = 0;

	if(CURRENT_SLOT_GET() == DUALMODE_SLOT_ZIGBEE && app_zigbeeIdle()){
		if(APP_BLE_STATE_GET() == BLS_LINK_STATE_IDLE){
			app_wakeup_config();
			drv_pm_lowPowerEnter();
		}
	}
}

#endif /* PM_ENABLE */
