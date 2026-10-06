#ifndef _APP_CFG_H_
#define _APP_CFG_H_

#include "version_cfg.h"

#define DEBUG_ENABLE					0
#define UART_PRINTF_MODE				0
#define USB_PRINTF_MODE					0

/* suspend between BLE events (advertising / connection) */
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
	EV_POLL_IDLE,
	EV_POLL_MAX,
}ev_poll_e;

#endif /* _APP_CFG_H_ */
