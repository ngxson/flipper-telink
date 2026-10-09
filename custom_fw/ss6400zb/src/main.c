/**********************************************************************
 * SS6400zb / TS0044 and TS0046 (ZT3L, TLSR8258 1 MB) - custom firmware.
 *
 * The mode is picked once, when the battery goes in:
 *
 *   button 1 held while the battery goes in -> SERVICE MODE: BLE only -
 *   Telink OTA for firmware updates, log and text commands over the
 *   debug service, the HLK-ZW101 fingerprint demo (fp.c). LED flashes
 *   every 2 s while advertising, every 0.5 s connected, solid during
 *   OTA. Leave it by pulling the battery.
 *
 *   otherwise -> ZIGBEE MODE (zb_dev.c): a sleepy end device that
 *   Zigbee2MQTT takes for the stock Tuya TS0044 / TS0046. Hold button 1
 *   for 10 s to (re-)pair.
 *
 * Only a real power-up (MCU_STATUS_BOOT) checks the button: wakes from
 * deep sleep come back through main() too, and a button press is what
 * wakes the remote.
 **********************************************************************/
#include "tl_common.h"
#include "stack/ble/ble.h"
#include "ble_cfg.h"
#include "ble_ota.h"
#include "log.h"
#include "fp.h"
#include "buttons.h"
#include "zb_dev.h"

/* BLE service mode (irq.c and buttons.c dispatch on it). In retention
 * RAM, so it stays 0 across Zigbee-mode deep sleeps */
u8 g_serviceMode;

/* button 1 must read low this long right after power-up for service mode */
#define BOOT_SERVICE_HOLD_MS	50

/* LED blip state, shared with zb_dev.c */
u32 led_tick;
u8 led_on;

#if PM_ENABLE

/* BLE service mode: periodic LED blink pattern (led_blip uses the same
 * state, one flash now) */
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

	/* wake up from suspend for the next LED edge or the next button
	 * hold threshold, whichever comes first (if before the next BLE event) */
	{
		u32 wake = led_tick + waitMs * 1000 * sysTimerPerUs;
		u32 btn = btn_wakeDeadline();
		if(btn && (s32)(btn - wake) < 0){
			wake = btn;
		}
		bls_pm_setAppWakeupLowPower(wake, 1);
	}
}

static void app_wakeupCb(int unused)
{
	(void)unused;
	led_task();
}

#endif /* PM_ENABLE */

/* one LED flash now (on a button event); the LED tasks turn it off */
void led_blip(void)
{
	led_on = 1;
	led_tick = clock_time();
	gpio_write(GPIO_LED, LED_ON);
}

/* ---- service mode (BLE: log, OTA, fp demo) ------------------------- */

static void service_bringUp(void)
{
	app_ota_init();			/* before user_ble_init: the strings show the running slot */
	user_ble_init();
	fp_init();

	/* drv_platform_init set the radio up for Zigbee */
	ble_radio_init();

#if PM_ENABLE
	bls_pm_registerAppWakeupLowPowerCb(app_wakeupCb);
#endif
}

static void service_mainLoop(void)
{
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
}

/* power-up with button 1 held? (pull-ups need a moment to charge) */
static u8 boot_serviceRequested(void)
{
	if(pm_get_mcu_status() != MCU_STATUS_BOOT){
		return 0;		/* wake from deep sleep, not a battery insert */
	}
	sleep_us(1000);
	for(int ms = 0; ms < BOOT_SERVICE_HOLD_MS; ms++){
		if(!btn_isDown(0)){
			return 0;
		}
		sleep_us(1000);
	}
	return 1;
}

int main(void)
{
	/* clocks, GPIO (board.h), 32k RC, flash size / MAC address */
	u8 isRetention = (drv_platform_init() == SYSTEM_RETENTION_EN) ? 1 : 0;

	if(!isRetention){
		log_init();
		btn_init();

		if(boot_serviceRequested()){
			g_serviceMode = 1;
			log_printf("boot v%02X.%02X service (button 1 held at power-up)", APP_RELEASE, APP_BUILD);
			btn_ignoreUntilRelease(0);
			service_bringUp();
			/* the BLE stack is initialized first, THEN the irq goes on
			 * (v00.11 lesson: irq_blt_sdk_handler on an uninitialized
			 * stack wedges the chip) */
			drv_enable_irq();
			service_mainLoop();
		}

		log_printf("boot v%02X.%02X zigbee", APP_RELEASE, APP_BUILD);
	}

	zb_main(isRetention);

	return 0;
}
