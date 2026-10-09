/**********************************************************************
 * Buttons (active low, 10k pull-ups): short press / long press (1 s) /
 * very long press (10 s), all dispatched with NO artificial delay -
 * unlike the stock firmware's 300 ms double-click window (see
 * ../README.md, "Why a button press takes ~500 ms").
 *
 *   short press:  fires on the release edge (the physical minimum)
 *   long press:   fires the moment 1 s of hold is reached
 *   very long:    fires the moment 10 s of hold is reached, plus a
 *                 "very long released" event on the release
 *
 * Layout: read at boot from the Tuya factory config at 0xF8000
 * (ch_num): 4 buttons = SS6400zb/TS0044, 6 = TS0046 (bt4 = PC4,
 * bt5 = PB4, bt6 = PC0 instead of PC3). Default 4 if no config.
 *
 * Sleeping: in IDLE the wake source is Level_Low (a press wakes at once);
 * while a button is held it is Level_High (the release wakes at once)
 * and btn_wakeDeadline() asks the service-mode PM code for a timed wake
 * at the next hold threshold. Zigbee mode simply stays awake while a
 * button is held (zb_dev.c).
 *
 * Actions:
 *   Zigbee mode: everything goes to zb_buttonEvent() (Tuya scene
 *                switch: short = single, long = hold; button 1 very long
 *                = pairing)
 *   service mode: button 1 short = enroll a fingerprint, button 2 short
 *                = delete all fingerprints, the rest is logged only
 **********************************************************************/
#include "tl_common.h"
#include "buttons.h"
#include "fp.h"
#include "log.h"
#include "zb_dev.h"

#define BTN_MAX				6
#define BTN_LONG_MS		1000	/* hold threshold: long press */
#define BTN_VLONG_MS		10000	/* hold threshold: very long press */
#define BTN_DEBOUNCE_MS		20		/* a press must last this long to count */

/* Tuya factory config: magic 0xdeadbeef, u32 CRC at +4, u16 text len at
 * +8, JSON text at +0xA (stock leaves it at 0xF8000; we never touch it) */
#define TUYA_CFG_ADDR		0xF8000
#define TUYA_CFG_MAX		512

enum{
	BTN_IDLE,		/* released, wake on low */
	BTN_DOWN,		/* held, short/long pending */
	BTN_HELD,		/* long fired, very long pending */
	BTN_VHELD,		/* very long fired, waiting for release */
	BTN_IGNORE,		/* held since boot (service mode entry): no events until released */
};

static const u32 btnPin4[4] = {BUTTON1, BUTTON2, BUTTON3, BUTTON4};
static const u32 btnPin6[6] = {BUTTON1, BUTTON2, BUTTON3, BUTTON4_TS0046,
							  BUTTON5_TS0046, BUTTON6_TS0046};
static u32 btnPin[BTN_MAX];
static u8 btnCount;
static u8 btnState[BTN_MAX];
static u32 btnPressTick[BTN_MAX];

extern void led_blip(void);	/* main.c: one LED flash now */
extern u8 g_serviceMode;	/* main.c: BLE service mode (fp demo actions only there) */

/* timed wake for the next hold threshold (system timer ticks), 0 = none.
 * Single deadline: held buttons are rare and BLE events wake the loop
 * anyway, so a second held button fires within one BLE event of its own
 * threshold at worst. */
static u32 btnWakeTick;
static u8 btnWakeEn;

/* PM (main.c led_task): absolute system-tick deadline to wake at, 0 = none */
u32 btn_wakeDeadline(void)
{
	return btnWakeEn ? btnWakeTick : 0;
}

/* main.c: a button is held - battery mode keeps the CPU running then
 * (holds are short and deliberate, and it gives SWS a window to catch) */
u8 btn_anyDown(void)
{
	for(int i = 0; i < btnCount; i++){
		if(btnState[i] != BTN_IDLE){
			return 1;
		}
	}
	return 0;
}

static void btn_setWake(int i, u32 ms)
{
	btnWakeTick = btnPressTick[i] + ms * 1000 * sysTimerPerUs;
	btnWakeEn = 1;
}

/* a button has been continuously down for >= ms (battery loop: btn 1 held
 * 3 s = switch to service mode) */
u8 btn_heldFor(int i, u32 ms)
{
	if(i >= btnCount){
		return 0;
	}
	u8 st = btnState[i];
	if(st == BTN_IDLE || st == BTN_IGNORE){
		return 0;
	}
	return clock_time_exceed(btnPressTick[i], ms * 1000);
}

static void btn_arm(int i, GPIO_LevelTypeDef pol, u8 on)
{
	cpu_set_gpio_wakeup(btnPin[i], pol, on);
}

static void btn_release(int i);
static void btn_glitch(int i);

/* read ch_num from the Tuya factory config JSON; 0 if absent/broken */
static int btn_cfgChNum(void)
{
	static _attribute_custom_bss_ u8 buf[10 + TUYA_CFG_MAX];	/* boot only: not in retention RAM */
	int len, i, j, n;

	flash_read_page(TUYA_CFG_ADDR, 10 + TUYA_CFG_MAX, buf);
	if(buf[0] != 0xef || buf[1] != 0xbe || buf[2] != 0xad || buf[3] != 0xde){
		return 0;
	}
	len = buf[8] | (buf[9] << 8);
	if(len <= 0 || len > TUYA_CFG_MAX - 1){
		return 0;
	}
	buf[10 + len] = 0;
	for(i = 10; i < 10 + len - 7; i++){
		if(buf[i] == 'c' && buf[i+1] == 'h' && buf[i+2] == '_' && buf[i+3] == 'n' &&
		   buf[i+4] == 'u' && buf[i+5] == 'm' && buf[i+6] == ':'){
			n = 0;
			for(j = i + 7; j < 10 + len && buf[j] >= '0' && buf[j] <= '9'; j++){
				n = n * 10 + (buf[j] - '0');
			}
			return n;
		}
	}
	return 0;
}

void btn_init(void)
{
	int ch = btn_cfgChNum();

	if(ch >= 5){			/* TS0046-style 6-button layout */
		btnCount = 6;
		for(int i = 0; i < 6; i++){
			btnPin[i] = btnPin6[i];
		}
	}else{					/* SS6400zb / TS0044 4-button layout (default) */
		btnCount = 4;
		for(int i = 0; i < 4; i++){
			btnPin[i] = btnPin4[i];
		}
	}
	log_printf("buttons: %u (Tuya config ch_num %d)", btnCount, ch);

	for(int i = 0; i < btnCount; i++){
		btnState[i] = BTN_IDLE;
		if(gpio_read(btnPin[i])){
			btn_arm(i, Level_Low, 1);
		}
	}
}

static void btn_action(int i, u8 kind)	/* BTN_EV_* */
{
	static const char *names[] = {"short", "long", "very long", "very long released"};

	log_printf("button %u %s", i + 1, names[kind]);
	if(kind != BTN_EV_VLONG_UP){
		led_blip();
	}

	if(!g_serviceMode){
		zb_buttonEvent(i, kind);
		return;
	}

	/* service mode: fingerprint demo */
	if(kind == BTN_EV_SHORT){
		switch(i){
		case 0: fp_enroll(); break;
		case 1: fp_clearAll(); break;
		default: break;
		}
	}
}

/* button i is down right now: swallow this press entirely (service mode
 * was entered by holding button 1 at power-up; its release must not
 * count as a click) */
void btn_ignoreUntilRelease(int i)
{
	if(i >= btnCount){
		return;
	}
	btn_arm(i, Level_Low, 0);
	btn_arm(i, Level_High, 1);
	btnState[i] = BTN_IGNORE;
}

/* raw level, for the power-up check (before btn_task has run) */
u8 btn_isDown(int i)
{
	return (i < btnCount && !gpio_read(btnPin[i])) ? 1 : 0;
}

/* Zigbee mode, before sleeping: every released button wakes on low */
void btn_armWakeAll(void)
{
	for(int i = 0; i < btnCount; i++){
		if(btnState[i] == BTN_IDLE){
			btn_arm(i, Level_High, 0);
			btn_arm(i, Level_Low, 1);
		}
	}
}

u8 btn_count(void)
{
	return btnCount;
}

void btn_task(void)
{
	for(int i = 0; i < btnCount; i++){
		u8 down = gpio_read(btnPin[i]) ? 0 : 1;

		switch(btnState[i]){
		case BTN_IDLE:
			if(down){
				btn_arm(i, Level_Low, 0);
				btn_arm(i, Level_High, 1);	/* wake on release */
				btnPressTick[i] = clock_time();
				btn_setWake(i, BTN_LONG_MS);
				btnState[i] = BTN_DOWN;
			}
			break;

		case BTN_DOWN:
			if(down){
				if(clock_time_exceed(btnPressTick[i], BTN_LONG_MS * 1000)){
					btn_setWake(i, BTN_VLONG_MS);
					btnState[i] = BTN_HELD;
					btn_action(i, BTN_EV_LONG);
				}
			}else if(!clock_time_exceed(btnPressTick[i], BTN_DEBOUNCE_MS * 1000)){
				btn_glitch(i);		/* too short to be a press */
			}else{
				btn_release(i);
				btn_action(i, BTN_EV_SHORT);
			}
			break;

		case BTN_HELD:
			if(down){
				if(clock_time_exceed(btnPressTick[i], BTN_VLONG_MS * 1000)){
					btnWakeEn = 0;		/* last threshold passed */
					btnState[i] = BTN_VHELD;
					btn_action(i, BTN_EV_VLONG);
				}
			}else{
				btn_release(i);
			}
			break;

		case BTN_VHELD:
			if(!down){
				btn_release(i);
				btn_action(i, BTN_EV_VLONG_UP);
			}
			break;

		case BTN_IGNORE:
			if(!down){
				btn_release(i);
			}
			break;
		}
	}
}

/* release edge seen: back to idle, wake on the next press */
static void btn_release(int i)
{
	btnWakeEn = 0;
	btn_arm(i, Level_High, 0);
	btn_arm(i, Level_Low, 1);
	btnState[i] = BTN_IDLE;
}

/* contact glitch (held < BTN_DEBOUNCE_MS): back to idle, no event */
static void btn_glitch(int i)
{
	btnWakeEn = 0;
	btn_arm(i, Level_High, 0);
	btn_arm(i, Level_Low, 1);
	btnState[i] = BTN_IDLE;
}
