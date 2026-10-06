/**********************************************************************
 * SS6400zb / TS0044 (ZT3L, TLSR8258 1 MB) - bring-up firmware.
 *
 * BLE only: blinks the network LED (PD7) and accepts new firmware over
 * BLE (Telink OTA, see ble_ota.c), so later images no longer need SWS.
 * Fingerprint demo with an HLK-ZW101 module (fp.c): touch = identify
 * (green 1 s match / red 1 s), button 1 = enroll (blinks blue), button 2 =
 * delete all (yellow). A log and text commands are available over BLE
 * (debug service, app_ble.c).
 *
 * LED: short flash every 2 s while advertising, every 0.5 s while
 * connected, solid on during an OTA transfer.
 **********************************************************************/
#include "tl_common.h"
#include "stack/ble/ble.h"
#include "ble_cfg.h"
#include "ble_ota.h"
#include "log.h"
#include "fp.h"
#include "buttons.h"

/* normally in drv_nv.c (not linked, no NV yet); drv_platform_init fixes
 * them up from the flash JEDEC id */
u32 g_u32MacFlashAddr = FLASH_ADDR_OF_MAC_ADDR_1M;
u32 g_u32CfgFlashAddr = FLASH_ADDR_OF_F_CFG_INFO_1M;

static u32 led_tick;
static u8 led_on;

static void led_task(void)
{
	u32 waitMs;

	if(ota_busy){
		return;		/* solid on, set by the OTA start callback */
	}

	if(led_on){
		waitMs = LED_ON_MS;
	}else{
		waitMs = (device_in_connection_state ? LED_PERIOD_CONN_MS : LED_PERIOD_MS) - LED_ON_MS;
	}

	if(clock_time_exceed(led_tick, waitMs * 1000)){
		led_tick = clock_time();
		led_on ^= 1;
		gpio_write(GPIO_LED, led_on ? LED_ON : LED_OFF);
		waitMs = led_on ? LED_ON_MS : (device_in_connection_state ? LED_PERIOD_CONN_MS : LED_PERIOD_MS) - LED_ON_MS;
	}

#if PM_ENABLE
	/* wake up from suspend for the next LED edge (if it comes before the
	 * next BLE event) */
	bls_pm_setAppWakeupLowPower(led_tick + waitMs * 1000 * sysTimerPerUs, 1);
#endif
}

/* one LED flash now (e.g. on a touch); led_task turns it off */
void led_blip(void)
{
	led_on = 1;
	led_tick = clock_time();
	gpio_write(GPIO_LED, LED_ON);
}

#if PM_ENABLE
static void app_wakeupCb(int unused)
{
	(void)unused;
	led_task();
}
#endif

int main(void)
{
	drv_platform_init();	/* clocks, GPIO (board.h), 32k RC, flash size / MAC address */

	log_init();
	app_ota_init();			/* before user_ble_init: the strings show the running slot */
	log_printf("boot v%02X.%02X @0x%05X", APP_RELEASE, APP_BUILD, ota_runningAddr);
	user_ble_init();
	fp_init();
	btn_init();

	/* drv_platform_init set the radio up for Zigbee */
	ble_radio_init();

#if PM_ENABLE
	bls_pm_registerAppWakeupLowPowerCb(app_wakeupCb);
#endif

	led_tick = clock_time();

	drv_enable_irq();

	while(1){
		blt_sdk_main_loop();
		log_tick();
		app_ota_task();
		btn_task();
		fp_task();
		app_ble_task();
		led_task();

#if PM_ENABLE
		/* the UART needs clocks while the module is powered */
		bls_pm_setSuspendMask((ota_busy || fp_busy()) ? SUSPEND_DISABLE : (SUSPEND_ADV | SUSPEND_CONN));
#endif
	}

	return 0;
}
