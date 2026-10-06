#ifndef _APP_CFG_H_
#define _APP_CFG_H_

#include "version_cfg.h"

#define DEBUG_ENABLE					0
#define UART_PRINTF_MODE				0
#define USB_PRINTF_MODE					0
#define ZBHCI_BLE						0

#define PM_ENABLE						1
#define PA_ENABLE						0

#define CLOCK_SYS_CLOCK_HZ				24000000

#include "board.h"

/* Battery */
#define VOLTAGE_DETECT_ENABLE			0
#define VOLTAGE_DETECT_ADC_PIN			GPIO_VBAT
#define BATTERY_SAFETY_THRESHOLD		2200	// mV
#define BATTERY_LOW_POWER				2000	// mV
#define LOW_POWER_SLEEP_TIME_ms			(60*1000)
#define USE_READ_ADC_CALIBRATION		0

#define MODULE_WATCHDOG_ENABLE			0
#define MODULE_UART_ENABLE				0

#define ZB_DEFAULT_TX_POWER_IDX			RF_POWER_P1p99dBm
#define BLE_DEFAULT_TX_POWER_IDX		RF_POWER_P1p99dBm

/* ZCL */
#define TOUCHLINK_SUPPORT				0
#define FIND_AND_BIND_SUPPORT			0
#define ZCL_POWER_CFG_SUPPORT			1
#define ZCL_POLL_CTRL_SUPPORT			0
#define ZCL_GROUP_SUPPORT				0
#define ZCL_OTA_SUPPORT					0
#define REJOIN_FAILURE_TIMER			1

/* BLE */
#define APP_SECURITY_ENABLE				0
#define APP_DIRECT_ADV_ENABLE			1
#define BLE_APP_PM_ENABLE				PM_ENABLE
#define USE_BLE_OTA						0
#define BLE_DEVICE_NAME_MAX				10

/* Application */
#define ZG228Z_ENDPOINT					1
#define ZG228Z_DEVICE_ID				0x0005	/* HA Simple Sensor */

/* End device poll rate (RX from parent), ms */
#define DEFAULT_POLL_RATE				(2 * 1000)	/* must stay well below the parent's ~7.7 s indirect-TX persistence, or commands from HA get dropped */

/* Vibration detection: pulse count window and threshold (stock: ~50 - sensitivity) */
#define VIB_WINDOW_MS					3000
#define VIB_THRESHOLD(sens)				(50 - (sens))

/* Default alarm behaviour */
#define DEF_ALARM_TIME_S					60		/* DP106 0..1800 */
#define DEF_SENSITIVITY					25		/* DP6 1..50 */
#define DEF_VIB_SIREN					0		/* DP101 OFF: no sound unless enabled from HA */
#define DEF_MELODY_SLOT					0		/* DP103 "low" = slot 1 */
#define DEF_ALARM_RING					1		/* DP104 "beep" */

/* Long button press to leave the network and re-pair */
#define BTN_LEAVE_NETWORK_MS				3000

/* Report battery every 6 hours */
#define BATTERY_REPORT_PERIOD_S			(6 * 3600)

/* RTTTL melody slots */
#define RTTTL_MAX_LEN					192
#define MELODY_SLOTS					3

/* NV version of the application settings (bump to reset module NV) */
#define USE_NV_APP						0x00000001

/* EV poll event ids (the SDK's os layer expects the app to define this) */
typedef enum{
	EV_POLL_ED_DETECT,		/* used by the MAC layer (energy detect) */
	EV_POLL_IDLE,
	EV_POLL_MAX,
}ev_poll_e;

/* NV items for the application (module NV_MODULE_APP) */typedef enum{
	NV_ITEM_APP_VER		= 0x5f,		/* settings layout version */
	NV_ITEM_APP_CFG		= 0x60,		/* alarm settings (DPs) */
	NV_ITEM_APP_MELODY1	= 0x61,		/* RTTTL slot 1 */
	NV_ITEM_APP_MELODY2	= 0x62,		/* RTTTL slot 2 */
	NV_ITEM_APP_MELODY3	= 0x63,		/* RTTTL slot 3 */
} app_nvItemId_t;

/* Zigbee stack tuning (stack_cfg.h uses PM_ENABLE etc. above) */
#include "stack_cfg.h"

#endif /* _APP_CFG_H_ */
