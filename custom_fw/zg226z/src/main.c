/**********************************************************************
 * ZG-228Z (HOBEIAN vibration alarm, TLSR8258) - custom firmware entry
 * point. Zigbee (Tuya datapoints over cluster 0xEF00) with a BLE mode
 * selected by bridging the TX pad (PB1) to the RX pad (PB7).
 **********************************************************************/
#include "tl_common.h"
#include "zb_common.h"
#include "device.h"
#include "app.h"
#include "app_cfg.h"
#include "app_ui.h"
#include "buzzer.h"
#include "ble_cfg.h"
#include "zigbee_ble_switch.h"
#include "stack/ble/ble.h"
#include "stack/ble/ble_config.h"
#include "stack/ble/ble_common.h"

extern void user_zb_init(bool isRetention);
extern void user_ble_init(bool isRetention);

int main(void)
{
	u8 isRetention = drv_platform_init();

	os_init(isRetention);

	/* PWM clock is needed for the buzzer (PC1) and the dimmed LED (PD4) */
	reg_clk_en0 = FLD_CLK0_SPI_EN
#if UART_PRINTF_MODE
			| FLD_CLK0_UART_EN
#endif
			| FLD_CLK0_PWM_EN
			| FLD_CLK0_SWIRE_EN;

	user_zb_init(isRetention);
	user_ble_init(isRetention);

	/* retention wake: the melody buffers are normal RAM, reload them */
	if(isRetention){
		buzzer_melodyInit();
	}

	/* BLE mode? (TX pad PB1 bridged to RX pad PB7 at boot) */
	if(ble_bridge_detect()){
		app_ble_enterBleOnly();
		ble_radio_init();
	}else{
		switch_to_zb_context();
	}

	drv_enable_irq();

#if (MODULE_WATCHDOG_ENABLE)
	drv_wd_setInterval(600);
	drv_wd_start();
#endif

	while(1){
		if(g_appCtx.bleOnly){
			/* BLE-only mode: run the BLE stack plus the app timers/LED
			 * directly (the Zigbee event loop is not running) */
			blt_sdk_main_loop();

			ev_timer_process(FALSE);
			ev_poll_process();
			app_task();

			app_ble_task();
		}else{
			concurrent_mode_main_loop();
			app_task();
		}

		task_keys();
		task_vibration();
		task_bleSwitch();

#if PM_ENABLE
		app_pm_task();
#endif
	}

	return 0;
}
