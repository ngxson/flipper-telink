#ifndef _APP_UI_H_
#define _APP_UI_H_

#include "tl_common.h"

/* LED (PD4, active high) */
void led_on(void);
void led_off(void);
void led_blink_start(u8 times, u16 onMs, u16 offMs);	/* times = 0: until stopped */
void led_blink_stop(void);

/* Poll the button (PD3) and the vibration sensor (PA0) */
void task_keys(void);
void task_vibration(void);
void task_bleSwitch(void);
bool ble_bridge_detect(void);

/* Configure the wake sources for low power sleep */
void app_wakeup_config(void);

#endif /* _APP_UI_H_ */
