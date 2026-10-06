/**********************************************************************
 * Battery measurement for the ZG-228Z: PC5 ADC channel (C5P), the same
 * circuit/trick as pvvx/BZdevice's TH03Z board.
 **********************************************************************/
#include "tl_common.h"
#include "device.h"
#include "app_cfg.h"
#include "app.h"
#include "buzzer.h"
#include "app_ui.h"
#include "battery.h"

static u32 batSum;
static u32 batCnt;

void battery_detect(bool startup)
{
	u16 threshold = startup ? BATTERY_SAFETY_THRESHOLD : BATTERY_LOW_POWER;
	u16 mv;
	u16 level;

	adc_channel_init(SHL_ADC_VBAT);
	mv = get_adc_mv(0);

	if(mv < threshold){
		/* battery nearly empty: sleep and wait for a fresh battery */
		buzzer_stop();
		led_off();
		drv_pm_longSleep(PM_SLEEP_MODE_DEEPSLEEP, PM_WAKEUP_SRC_PAD, LOW_POWER_SLEEP_TIME_ms);
	}

	/* running average, mainly to smooth out single noisy reads */
	batSum += mv;
	if(batCnt < 16){
		batCnt++;
	}
	mv = (u16)(batSum / batCnt);

	g_appCtx.battery_mv = mv;

	/* 2200 mV .. 3000 mV -> 0..100 % */
	if(mv <= BATTERY_SAFETY_THRESHOLD){
		level = 0;
	}else if(mv >= 3000){
		level = 100;
	}else{
		level = ((u32)(mv - BATTERY_SAFETY_THRESHOLD) * 100) / (3000 - BATTERY_SAFETY_THRESHOLD);
	}

	app_setBattery(mv, (u8)level);
}
