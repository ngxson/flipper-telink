#ifndef _APP_CFG_H_
#define _APP_CFG_H_

#include "version_cfg.h"

#define DEBUG_ENABLE					0
#define UART_PRINTF_MODE				0
#define USB_PRINTF_MODE					0

/* service mode: suspend between BLE events; Zigbee mode: deep sleep with
 * retention between polls */
#define PM_ENABLE						1
#define PA_ENABLE						0

#define CLOCK_SYS_CLOCK_HZ				24000000

/* 1 MB flash: MAC at 0xFF000, calibration at 0xFE000 (both detected at
 * runtime from the JEDEC id), BLE NV at 0xF7000 */
#define FLASH_CAP_SIZE_1M				1

#include "board.h"

#define USE_READ_ADC_CALIBRATION		0
#define MODULE_WATCHDOG_ENABLE			0
#define MODULE_UART_ENABLE				0


#define BLE_DEFAULT_TX_POWER_IDX		RF_POWER_P1p99dBm

/* ---- Zigbee mode (normal operation): Tuya TS0044 / TS0046 clone ---- */

/* battery: PC5 VBAT trick (see board.h), 2.5..3.0 V like the stock config */
#define VOLTAGE_DETECT_ENABLE			0
#define BATTERY_MV_MIN					2500
#define BATTERY_MV_MAX					3000
#define BATTERY_REPORT_PERIOD_S			(4 * 3600)

/* ZCL */
#define TOUCHLINK_SUPPORT				0
#define FIND_AND_BIND_SUPPORT			0
#define ZCL_POWER_CFG_SUPPORT			1
#define ZCL_ON_OFF_SUPPORT				0	/* on/off is only sent (Tuya 0xFD), never served */
#define ZCL_POLL_CTRL_SUPPORT			0
#define ZCL_GROUP_SUPPORT				0
#define ZCL_OTA_SUPPORT					0
#define REJOIN_FAILURE_TIMER			1

/* end device poll period (ms): fast while Z2M may talk to us (interview,
 * right after a button), slow otherwise - a remote never needs inbound data */
#define ZB_POLL_FAST_MS					250
#define ZB_POLL_SLOW_MS					(60 * 1000)
#define ZB_FAST_POLL_AFTER_JOIN_MS		(60 * 1000)	/* covers the Z2M interview + configure */
#define ZB_FAST_POLL_AFTER_KEY_MS		(10 * 1000)

/* network steering: a few attempts, then give up (10 s hold on button 1
 * starts it again) - never scan forever on a coin cell */
#define ZB_STEER_ATTEMPTS				4
#define ZB_REJOIN_BACKOFF_MS			(15 * 60 * 1000)

/* OTA: two image slots, the boot ROM starts whichever has the flag */
#define OTA_SLOT_ADDR					0x40000		/* second slot */
#define OTA_SLOT_SIZE_K					256			/* max image size */
#define OTA_TIMEOUT_US					(120 * 1000 * 1000)	/* whole transfer */

/* LED pattern: flash LED_ON_MS every LED_PERIOD_MS */
#define LED_ON_MS						40
#define LED_PERIOD_MS					2000		/* advertising */
#define LED_PERIOD_CONN_MS				500			/* connected */

/* EV poll event ids (the SDK's os layer expects the app to define this) */
typedef enum{
	EV_POLL_ED_DETECT,		/* used by the MAC layer (energy detect) */
	EV_POLL_IDLE,
	EV_POLL_MAX,
}ev_poll_e;

/* Zigbee stack tuning (uses PM_ENABLE above) */
#include "stack_cfg.h"

#endif /* _APP_CFG_H_ */
