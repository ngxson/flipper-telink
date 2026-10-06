#ifndef _APP_H_
#define _APP_H_

#include "tl_common.h"

/* Persisted settings (Tuya DPs 6, 101, 103, 104, 106) */
typedef struct{
	u8  sensitivity;	/* DP6:  1..50, higher = more sensitive */
	u8  vib_siren;		/* DP101: 0 = OFF, 1 = sound the alarm on vibration */
	u8  melody_slot;	/* DP103: 0..2 = melody slot 1..3, 3 = mute */
	u8  alarm_ring;		/* DP104: 0 mute, 1 beep, 2 music */
	u16 alarm_time;		/* DP106: 0..1800 s */
} app_cfg_t;

extern app_cfg_t g_appCfg;

void app_init(void);
void app_task(void);

/* Called by the Tuya cluster for a DP value from the gateway */
bool app_tuyaSetDp(u8 dp, u8 type, u16 len, u8 *data);
/* Report every DP (answer to data query) */
void app_tuyaReportAll(void);

/* Called by the vibration sensor task when the pulse threshold is reached */
void app_vibrationDetected(void);

/* Called by the button task */
void app_buttonShortPress(void);
void app_buttonLongPress(void);

/* Battery (called by battery.c) */
void app_setBattery(u16 mv, u8 pct);

/* TRUE while the alarm or the melody player is running (keep the CPU awake) */
bool app_isBusy(void);

/* Low power sleep task (app_pm.c, PM_ENABLE only) */
void app_pm_task(void);

#endif /* _APP_H_ */
