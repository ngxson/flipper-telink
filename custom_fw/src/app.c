/**********************************************************************
 * ZG-228Z application core: persisted DP settings, alarm state machine,
 * battery reporting. All Tuya DP semantics follow
 * zigbee-herdsman-converters src/devices/hobeian.ts (ZG-228Z).
 **********************************************************************/
#include "tl_common.h"
#include "device.h"
#include "zb_api.h"
#include "app.h"
#include "app_cfg.h"
#include "app_ui.h"
#include "buzzer.h"
#include "tuya_cluster.h"
#include "battery.h"

#define NV_ITEM_CFG_VER					((u8)NV_ITEM_APP_VER)

app_cfg_t g_appCfg;

static const app_cfg_t app_cfgDefault = {
	.sensitivity	= DEF_SENSITIVITY,
	.vib_siren		= DEF_VIB_SIREN,
	.melody_slot	= DEF_MELODY_SLOT,
	.alarm_ring		= DEF_ALARM_RING,
	.alarm_time		= DEF_ALARM_TIME_S,
};

/* Beep alarm pattern (looped while the alarm sounds) */
#define BEEP_MELODY		"beep:d=8,o=7,b=160:g,p,p,p,p,p,p,p"
/* Short button preview beep */
#define BEEP_ONCE		"beep:d=8,o=7,b=160:g"

/*
 * Runtime state
 */
typedef struct{
	u8  alarmActive;		/* buzzer/LED alarm running */
	u8  alarmManual;		/* stopped by the user (vs. timed out) */
	u8  vibReported;		/* DP1 reported as 1, waiting to be cleared */
	u8  beepOncePending;	/* DP105 "beep": report "stop" when the sound ends */
	u16 alarmEndSec;
	u16 vibClearSec;		/* uptime (s) when DP1 should be cleared */
	u16 nextBatterySec;	/* uptime (s) of the next battery report */
} app_run_t;

static app_run_t r;

/**********************************************************************
 * Settings persistence (NV module 6)
 */
static void app_cfgSave(void)
{
	nv_flashWriteNew(1, NV_MODULE_APP, NV_ITEM_APP_CFG, sizeof(app_cfg_t), (u8 *)&g_appCfg);
}

static void app_cfgLoad(void)
{
	u32 ver = 0;
	u8 buf[sizeof(app_cfg_t)];

	if(nv_flashReadNew(1, NV_MODULE_APP, NV_ITEM_CFG_VER, sizeof(ver), (u8 *)&ver) == NV_SUCC
		&& ver == USE_NV_APP){
		if(nv_flashReadNew(1, NV_MODULE_APP, NV_ITEM_APP_CFG, sizeof(app_cfg_t), buf) == NV_SUCC){
			memcpy((u8 *)&g_appCfg, buf, sizeof(app_cfg_t));
		}else{
			memcpy((u8 *)&g_appCfg, &app_cfgDefault, sizeof(app_cfg_t));
		}
	}else{
		/* new layout: reset the app module and store defaults */
		nv_resetModule(NV_MODULE_APP);
		ver = USE_NV_APP;
		nv_flashWriteNew(1, NV_MODULE_APP, NV_ITEM_CFG_VER, sizeof(ver), (u8 *)&ver);
		g_appCfg = app_cfgDefault;
		app_cfgSave();
	}

	/* sanitize */
	if(g_appCfg.sensitivity < 1 || g_appCfg.sensitivity > 50){
		g_appCfg.sensitivity = DEF_SENSITIVITY;
	}
	g_appCfg.vib_siren = g_appCfg.vib_siren ? 1 : 0;
	if(g_appCfg.melody_slot > 3){
		g_appCfg.melody_slot = DEF_MELODY_SLOT;
	}
	if(g_appCfg.alarm_ring > 2){
		g_appCfg.alarm_ring = DEF_ALARM_RING;
	}
	if(g_appCfg.alarm_time > 1800){
		g_appCfg.alarm_time = DEF_ALARM_TIME_S;
	}
}

/**********************************************************************
 * Alarm state machine
 */

/* start the siren for alarm_time seconds (mode from DP104 / DP103) */
static void alarm_start(void)
{
	u16 dur = g_appCfg.alarm_time;
	if(dur == 0){
		dur = 5;	/* a zero alarm time would end immediately */
	}
	r.alarmActive = 1;
	r.alarmEndSec = g_appCtx.utc_sec + dur;
	r.alarmManual = 0;

	buzzer_stop();

	switch(g_appCfg.alarm_ring){
		case 1:	/* beep */
			buzzer_play(BEEP_MELODY, TRUE, 0);
			break;
		case 2:	/* music: melody slot selected by DP103 */
			if(g_appCfg.melody_slot < MELODY_SLOTS){
				buzzer_play(buzzer_getMelody(g_appCfg.melody_slot), TRUE, 0);
			}
			/* melody_slot 3 = mute: LED only */
			break;
		default:
			break;	/* mute: LED only */
	}

	led_blink_start(0, 100, 100);	/* fast LED for the whole alarm */
}

static void alarm_stop(u8 manual)
{
	if(!r.alarmActive){
		return;
	}
	r.alarmActive = 0;
	buzzer_stop();
	led_blink_stop();

	tuya_reportDpEnum(TUYA_DP_ALARM, 2);	/* "stop" */
	tuya_reportDpBool(TUYA_DP_MUFFLING, 0);
}

void app_vibrationDetected(void)
{
	r.vibReported = 1;
	r.vibClearSec = g_appCtx.utc_sec + (VIB_WINDOW_MS / 1000) + 1;

	tuya_reportDpEnum(TUYA_DP_VIBRATION, 1);

	/* siren only when enabled from HA (DP101) and actually on the network */
	if(g_appCfg.vib_siren && zb_isDeviceJoinedNwk() && !r.alarmActive){
		alarm_start();
	}
}

void app_buttonShortPress(void)
{
	if(r.alarmActive){
		alarm_stop(1);
		return;
	}
	/* no sound and no setting change from the button: settings come from HA only */
}

void app_buttonLongPress(void)
{
	/* leave the network and start commissioning again so it can re-pair */
	alarm_stop(1);
	tl_bdbReset2FN();
	zb_resetDevice();
}

bool app_isBusy(void)
{
	return r.alarmActive || buzzer_isPlaying();
}

void app_setBattery(u16 mv, u8 pct)
{
	g_appCtx.battery_mv = mv;
	g_appCtx.battery_level = pct;

	/* also publish on the standard power configuration cluster */
	g_zcl_powerAttrs.batteryVoltage = (u8)((mv + 50) / 100);
	g_zcl_powerAttrs.batteryPercentage = pct * 2;	/* 0.5 % units */

	if(zb_isDeviceJoinedNwk()){
		tuya_reportDpValue(TUYA_DP_BATTERY, pct);
	}
}

/**********************************************************************
 * Tuya datapoint handling
 */
bool app_tuyaSetDp(u8 dp, u8 type, u16 len, u8 *data)
{
	u32 val = 0;
	u8 v8 = 0;
	u8 i;

	for(i = 0; i < len && i < 4; i++){
		val = (val << 8) | data[i];
	}
	if(len){
		v8 = data[0];
	}

	switch(dp){
		case TUYA_DP_SENSITIVITY:
			if(type != TUYA_TYPE_VALUE){
				return FALSE;
			}
			if(val < 1){
				val = 1;
			}
			if(val > 50){
				val = 50;
			}
			g_appCfg.sensitivity = (u8)val;
			app_cfgSave();
			tuya_reportDpValue(TUYA_DP_SENSITIVITY, val);
			return TRUE;

		case TUYA_DP_VIB_SIREN:
			if(type != TUYA_TYPE_ENUM){
				return FALSE;
			}
			g_appCfg.vib_siren = v8 ? 1 : 0;
			app_cfgSave();
			tuya_reportDpEnum(TUYA_DP_VIB_SIREN, g_appCfg.vib_siren);
			return TRUE;

		case TUYA_DP_MUFFLING:
			if(type != TUYA_TYPE_BOOL){
				return FALSE;
			}
			if(v8){
				alarm_stop(1);
			}
			return TRUE;

		case TUYA_DP_ALARM_VOLUME:
			if(type != TUYA_TYPE_ENUM || v8 > 3){
				return FALSE;
			}
			g_appCfg.melody_slot = v8;
			app_cfgSave();
			tuya_reportDpEnum(TUYA_DP_ALARM_VOLUME, v8);
			return TRUE;

		case TUYA_DP_ALARM_RING:
			if(type != TUYA_TYPE_ENUM || v8 > 2){
				return FALSE;
			}
			g_appCfg.alarm_ring = v8;
			app_cfgSave();
			tuya_reportDpEnum(TUYA_DP_ALARM_RING, v8);
			return TRUE;

		case TUYA_DP_ALARM:
			if(type != TUYA_TYPE_ENUM){
				return FALSE;
			}
			switch(v8){
				case 0:	/* beep: play the configured sound once, then report "stop" */
					if(!r.alarmActive){
						if(g_appCfg.alarm_ring == 1){
							buzzer_play(BEEP_ONCE, FALSE, 0);
						}else if(g_appCfg.alarm_ring == 2 && g_appCfg.melody_slot < MELODY_SLOTS){
							buzzer_play(buzzer_getMelody(g_appCfg.melody_slot), FALSE, 0);
						}
						r.beepOncePending = 1;	/* mute: nothing plays, "stop" right away */
					}
					break;
				case 1:	/* ring */
					if(!r.alarmActive){
						alarm_start();
					}
					break;
				case 2:	/* stop */
					alarm_stop(1);
					break;
			}
			return TRUE;

		case TUYA_DP_ALARM_TIME:
			if(type != TUYA_TYPE_VALUE){
				return FALSE;
			}
			if(val > 1800){
				val = 1800;
			}
			g_appCfg.alarm_time = (u16)val;
			app_cfgSave();
			tuya_reportDpValue(TUYA_DP_ALARM_TIME, val);
			return TRUE;

		default:
			/* DP1 (vibration) and DP4 (battery) are report-only */
			return FALSE;
	}
}

void app_tuyaReportAll(void)
{
	tuya_reportDpEnum(TUYA_DP_VIBRATION, r.vibReported ? 1 : 0);
	tuya_reportDpValue(TUYA_DP_BATTERY, g_appCtx.battery_level);
	tuya_reportDpValue(TUYA_DP_SENSITIVITY, g_appCfg.sensitivity);
	tuya_reportDpEnum(TUYA_DP_VIB_SIREN, g_appCfg.vib_siren);
	tuya_reportDpBool(TUYA_DP_MUFFLING, 0);
	tuya_reportDpEnum(TUYA_DP_ALARM_VOLUME, g_appCfg.melody_slot);
	tuya_reportDpEnum(TUYA_DP_ALARM_RING, g_appCfg.alarm_ring);
	tuya_reportDpEnum(TUYA_DP_ALARM, r.alarmActive ? 1 : 2);
	tuya_reportDpValue(TUYA_DP_ALARM_TIME, g_appCfg.alarm_time);
}

/**********************************************************************
 * Init / task
 */
void app_init(void)
{
	memset((u8 *)&r, 0, sizeof(r));

	app_cfgLoad();
	buzzer_init();

	g_appCtx.tuyaDstAddr = 0xffff;
	g_appCtx.battery_level = 100;

	/* first battery measurement (report suppressed while not joined) */
	battery_detect(TRUE);
}

void app_task(void)
{
	if(g_appCtx.bleOnly){
		/* BLE mode: the RF lives in the BLE slot, no Zigbee traffic from here */
		return;
	}

	/* uptime in seconds */
	while(g_appCtx.secTimeTik == 0 || (clock_time() - g_appCtx.secTimeTik) >= CLOCK_16M_SYS_TIMER_CLK_1S){
		if(g_appCtx.secTimeTik == 0){
			g_appCtx.secTimeTik = clock_time();
		}else{
			g_appCtx.secTimeTik += CLOCK_16M_SYS_TIMER_CLK_1S;
		}
		g_appCtx.utc_sec++;
	}

	/* DP105 "beep" finished: flip the enum back to "stop" */
	if(r.beepOncePending && !buzzer_isPlaying()){
		r.beepOncePending = 0;
		tuya_reportDpEnum(TUYA_DP_ALARM, 2);
	}

	/* clear the vibration DP after the detection window */
	if(r.vibReported && (s16)(g_appCtx.utc_sec - r.vibClearSec) >= 0){
		r.vibReported = 0;
		tuya_reportDpEnum(TUYA_DP_VIBRATION, 0);
	}

	/* alarm timeout */
	if(r.alarmActive && (s16)(g_appCtx.utc_sec - r.alarmEndSec) >= 0){
		alarm_stop(0);
	}

	/* battery: measure + report periodically */
	if(r.nextBatterySec == 0 || (s16)(g_appCtx.utc_sec - r.nextBatterySec) >= 0){
		r.nextBatterySec = g_appCtx.utc_sec + BATTERY_REPORT_PERIOD_S;
		battery_detect(FALSE);
	}
}
