/*
 * ble_cfg.h - BLE configuration and GATT handles
 */
#ifndef SRC_BLE_CFG_H_
#define SRC_BLE_CFG_H_

#include "app_cfg.h"

#define DEF_APP_ADV_CHANNEL				BLT_ENABLE_ADV_ALL
#define DEF_ADV_INTERVAL_MIN			800		/* 500 ms * 1.6 */
#define DEF_ADV_INTERVAL_MAX			(DEF_ADV_INTERVAL_MIN + 16)

/* the OTA packet format is fixed to 20-byte writes, the default MTU fits */
#define MTU_SIZE_SETTING				23

/* debug service: Bluetooth base UUID with these 16-bit ids */
#define DBG_SVC_UUID16					0xFFF0
#define DBG_LOG_UUID16					0xFFF1	/* notify: log text, streamed from the oldest line on subscribe */
#define DBG_CMD_UUID16					0xFFF2	/* write: text command (see fp_command, plus "dump", "clear") */

#define DEV_NAME_PREFIX					"SS6400_"	/* + 4 MAC hex digits */
#define DEV_NAME_LEN					11

/*
 * GATT handle map. The attribute table lists attributes in exactly this
 * order, so the enum doubles as the ATT handle.
 */
typedef enum{
	ATT_H_START = 0,

	/* Generic Access */
	GenericAccess_PS_H,
	GenericAccess_DeviceName_CD_H,
	GenericAccess_DeviceName_DP_H,
	GenericAccess_Appearance_CD_H,
	GenericAccess_Appearance_DP_H,

	/* GATT */
	GenericAttribute_PS_H,
	GenericAttribute_ServiceChanged_CD_H,
	GenericAttribute_ServiceChanged_DP_H,
	GenericAttribute_ServiceChanged_CCB_H,

	/* Device Information: firmware revision */
	DevInfo_PS_H,
	DevInfo_FwRev_CD_H,
	DevInfo_FwRev_DP_H,

	/* Telink OTA */
	OTA_PS_H,
	OTA_CMD_OUT_CD_H,
	OTA_CMD_OUT_DP_H,
	OTA_CMD_OUT_DESC_H,

	/* debug service: log (notify) + text commands (write) */
	DBG_PS_H,
	DBG_LOG_CD_H,
	DBG_LOG_DP_H,
	DBG_LOG_CCC_H,
	DBG_CMD_CD_H,
	DBG_CMD_DP_H,

	ATT_END_H
} ATT_HANDLE;

extern u8 mac_public[6];
extern int device_in_connection_state;

void user_ble_init(void);
void app_ble_task(void);	/* main loop: run queued commands, stream the log */

#endif /* SRC_BLE_CFG_H_ */
