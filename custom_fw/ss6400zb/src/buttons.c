/**********************************************************************
 * Buttons (active low, 10k pull-ups): a press is the falling edge while
 * armed; re-armed after BTN_RELEASE_MS of released. Armed buttons are
 * suspend wake sources, so a press is seen at once.
 *
 *   button 1: enroll a new fingerprint
 *   button 2: delete all fingerprints
 *   button 3, 4: logged only
 **********************************************************************/
#include "tl_common.h"
#include "buttons.h"
#include "fp.h"
#include "log.h"

#define BTN_COUNT			4
#define BTN_RELEASE_MS		50

extern void led_blip(void);

static const u32 btnPin[BTN_COUNT] = {BUTTON1, BUTTON2, BUTTON3, BUTTON4};
static u8 btnArmed[BTN_COUNT];
static u32 btnUpTick[BTN_COUNT];

static void btn_arm(int i, u8 on)
{
	btnArmed[i] = on;
	cpu_set_gpio_wakeup(btnPin[i], Level_Low, on);
}

void btn_init(void)
{
	for(int i = 0; i < BTN_COUNT; i++){
		btnUpTick[i] = clock_time();
		btn_arm(i, gpio_read(btnPin[i]) ? 1 : 0);
	}
}

static void btn_pressed(int i)
{
	log_printf("button %u", i + 1);
	led_blip();
	switch(i){
		case 0: fp_enroll(); break;
		case 1: fp_clearAll(); break;
		default: break;
	}
}

void btn_task(void)
{
	for(int i = 0; i < BTN_COUNT; i++){
		u8 down = gpio_read(btnPin[i]) ? 0 : 1;
		if(down){
			btnUpTick[i] = clock_time();
			if(btnArmed[i]){
				btn_arm(i, 0);
				btn_pressed(i);
			}
		}else if(!btnArmed[i] && clock_time_exceed(btnUpTick[i], BTN_RELEASE_MS * 1000)){
			btn_arm(i, 1);
		}
	}
}
