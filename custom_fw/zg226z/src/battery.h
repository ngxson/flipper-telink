#ifndef _BATTERY_H_
#define _BATTERY_H_

#include "tl_common.h"

/* ADC helpers live in src/patch_sdk/adc_drv.c */
void adc_channel_init(ADC_InputPchTypeDef p_ain);
u16 get_adc_mv(int flg);

/* Sample VBAT (PC5, same trick as pvvx/BZdevice TH03Z), update the app
 * context and report DP4. Call with startup = TRUE from boot. */
void battery_detect(bool startup);

#endif /* _BATTERY_H_ */
