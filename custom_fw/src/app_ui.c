/**********************************************************************
 * ZG-228Z UI: LED (PD4), button (PD3), vibration sensor (PA0),
 * BLE-mode switch (TX pad PB1 bridged to RX pad PB7).
 **********************************************************************/
#include "tl_common.h"
#include "device.h"
#include "app_cfg.h"
#include "board.h"
#include "app.h"
#include "app_ui.h"
#include "zcl_include.h"

/**********************************************************************
 * LED
 */
/* LED dimmed to ~10 % with PWM2_N on PD4 (1 kHz). The PWM clock divider is
 * shared with the buzzer, which uses the same 1 MHz tick. Run at 25 kHz so the
 * LED drive is not audible. *_N outputs are
 * inverted, so 10 % on-time needs cmp = 90 % of the cycle. */
#define LED_PWM_ID			PWM2_ID
#define LED_PWM_FUNC		AS_PWM2_N
#define LED_PWM_CLK_HZ		1000000
#define LED_PWM_CYCLE		40		/* 1 MHz / 40 = 25 kHz: inaudible */
#define LED_BRIGHTNESS_PCT	10

void led_on(void)
{
	pwm_set_clk(CLOCK_SYS_CLOCK_HZ, LED_PWM_CLK_HZ);
	pwm_set_cycle_and_duty(LED_PWM_ID, LED_PWM_CYCLE, LED_PWM_CYCLE * (100 - LED_BRIGHTNESS_PCT) / 100);
	gpio_set_func(GPIO_LED, LED_PWM_FUNC);
	pwm_start(LED_PWM_ID);
}

void led_off(void)
{
	pwm_stop(LED_PWM_ID);
	gpio_set_func(GPIO_LED, AS_GPIO);
	gpio_write(GPIO_LED, LED_OFF);
}

s32 ledTimerCb(void *arg)
{
	u32 interval;

	if(g_appCtx.times && g_appCtx.sta == g_appCtx.oriSta){
		/* counted down (blink "times" cycles) */
		g_appCtx.times--;
		if(g_appCtx.times == 0){
			g_appCtx.timerLedEvt = NULL;
			return -1;
		}
	}

	g_appCtx.sta = !g_appCtx.sta;
	if(g_appCtx.sta){
		led_on();
		interval = g_appCtx.ledOnTime;
	}else{
		led_off();
		interval = g_appCtx.ledOffTime;
	}
	return (s32)interval;
}

void led_blink_start(u8 times, u16 onMs, u16 offMs)
{
	g_appCtx.times = times;

	if(!g_appCtx.timerLedEvt){
		g_appCtx.ledOnTime = onMs;
		g_appCtx.ledOffTime = offMs;
		g_appCtx.oriSta = 0;
		led_on();
		g_appCtx.sta = 1;
		g_appCtx.timerLedEvt = TL_ZB_TIMER_SCHEDULE(ledTimerCb, NULL, onMs);
	}
}

void led_blink_stop(void)
{
	if(g_appCtx.timerLedEvt){
		TL_ZB_TIMER_CANCEL(&g_appCtx.timerLedEvt);
		g_appCtx.timerLedEvt = NULL;
	}
	led_off();
}

/**********************************************************************
 * Button (PD3, active low)
 *
 * short press  - stop a sounding alarm, otherwise cycle DP104 (alarm ring)
 *                and play a short preview
 * hold ~3 s    - leave the network and rejoin (so it can pair again)
 */
static s32 keyTimerCb(void *arg)
{
	u8 buttonDown = gpio_read(BUTTON1) ? 0 : 1;

	if(!buttonDown){
		g_appCtx.timerKeyEvt = NULL;
		g_appCtx.keyPressed = 0;
		return -1;
	}

	if(g_appCtx.keyPressed && clock_time_exceed(g_appCtx.keyPressedTime, BTN_LEAVE_NETWORK_MS * 1000)){
		g_appCtx.keyPressedTime = clock_time();
		g_appCtx.timerKeyEvt = NULL;
		g_appCtx.keyPressed = 0;
		app_buttonLongPress();
		return -1;
	}

	return 50;	/* keep polling while the button is held */
}

void task_keys(void)
{
	u8 buttonDown = gpio_read(BUTTON1) ? 0 : 1;

	if(buttonDown){
		if(!g_appCtx.keyPressed){
			g_appCtx.keyPressed = 1;
			g_appCtx.keyPressedTime = clock_time();
			if(!g_appCtx.timerKeyEvt){
				g_appCtx.timerKeyEvt = TL_ZB_TIMER_SCHEDULE(keyTimerCb, NULL, 100);
			}
		}
	}else{
		if(g_appCtx.keyPressed){
			g_appCtx.keyPressed = 0;
			if(g_appCtx.timerKeyEvt){
				TL_ZB_TIMER_CANCEL(&g_appCtx.timerKeyEvt);
				g_appCtx.timerKeyEvt = NULL;
				app_buttonShortPress();
			}
		}
	}
}

/**********************************************************************
 * Vibration sensor (PA0, 1M pull-up, level changes on vibration)
 *
 * The stock firmware counts level changes and reports vibration once the
 * count reaches roughly (50 - sensitivity) within a 3 second window.
 */
static struct{
	u32 windowStart;	/* tick when the current window started */
	u32 lastRead;
	u8  lastLevel;
	u16 pulses;
} vib;

void task_vibration(void)
{
	u8 level;

	if(g_appCtx.bleOnly){
		return;	/* BLE mode: no Zigbee reports */
	}

	level = gpio_read(GPIO_VIBRATION) ? 1 : 0;

	if(level != vib.lastLevel){
		vib.lastLevel = level;
		vib.lastRead = clock_time();
		if(vib.pulses == 0){
			vib.windowStart = vib.lastRead;
		}
		if(vib.pulses < 0xffff){
			vib.pulses++;
		}

		if(vib.pulses >= VIB_THRESHOLD(g_appCfg.sensitivity)){
			vib.pulses = 0;
			app_vibrationDetected();
		}
		return;
	}

	/* window expired: reset the counter */
	if(vib.pulses && clock_time_exceed(vib.windowStart, VIB_WINDOW_MS * 1000)){
		vib.pulses = 0;
	}
}

/**********************************************************************
 * Wake sources for low power sleep
 */
void app_wakeup_config(void)
{
	/* vibration pin: wake on the opposite of its current level, so a switch
	 * resting closed (pin low) does not keep the chip awake */
	cpu_set_gpio_wakeup(GPIO_VIBRATION, gpio_read(GPIO_VIBRATION) ? Level_Low : Level_High, 1);
	/* button: active low */
	cpu_set_gpio_wakeup(BUTTON1, Level_Low, 1);
}

/**********************************************************************
 * BLE mode switch: TX pad (PB1) bridged to RX pad (PB7)
 */
bool ble_bridge_detect(void)
{
	bool bridged = TRUE;
	int i;

	for(i = 0; i < 3 && bridged; i++){
		gpio_write(GPIO_BRIDGE_TX, 0);
		sleep_us(30);
		if(gpio_read(GPIO_BRIDGE_RX)){
			bridged = FALSE;
		}
		gpio_write(GPIO_BRIDGE_TX, 1);
		sleep_us(30);
		if(!gpio_read(GPIO_BRIDGE_RX)){
			bridged = FALSE;
		}
	}
	/* PB1 stays driven high: no current flows through the bridge */
	return bridged;
}

/* Zigbee mode: TX<->RX bridged -> reboot into BLE mode (checked once a second
 * while awake; the device wakes at least every poll, or on a button press) */
void task_bleSwitch(void)
{
	static u32 lastCheck;

	if(g_appCtx.bleOnly || !clock_time_exceed(lastCheck, 1000 * 1000)){
		return;
	}
	lastCheck = clock_time();
	if(ble_bridge_detect()){
		SYSTEM_RESET();
	}
}
