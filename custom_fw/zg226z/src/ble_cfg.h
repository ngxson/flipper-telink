/*
 * ble_cfg.h - BLE configuration and GATT handles for the ZG-228Z
 */
#ifndef SRC_BLE_CFG_H_
#define SRC_BLE_CFG_H_

#include "app_cfg.h"

#define DEF_APP_ADV_CHANNEL				BLT_ENABLE_ADV_ALL
#define DEF_ADV_INTERVAL_MIN			1600	/* 1000 ms * 1.6 */
#define DEF_ADV_INTERVAL_MAX			(DEF_ADV_INTERVAL_MIN + 10)

/* connection parameters: interval 50 ms, latency 19 (~1 s), timeout 4 s */
#define DEF_CON_PAR_UPDATE				40, 40, 19, 400

#define BLE_DEVICE_ADDRESS_TYPE			BLE_DEVICE_ADDRESS_PUBLIC

/* default MTU: the 40-byte TX FIFO cannot carry larger ATT responses;
 * long slot reads use Read Blob, writes are chunked */
#define MTU_SIZE_SETTING				23

/* custom service: Bluetooth base UUID with our 16-bit ids */
#define ZG228Z_SVC_UUID16		0xFFE0
#define ZG228Z_CHAR_PLAY_UUID16	0xFFE1	/* write: RTTTL to play right away */
#define ZG228Z_CHAR_SLOT1_UUID16	0xFFE2	/* read/write: melody slot 1 */
#define ZG228Z_CHAR_SLOT2_UUID16	0xFFE3	/* read/write: melody slot 2 */
#define ZG228Z_CHAR_SLOT3_UUID16	0xFFE4	/* read/write: melody slot 3 */

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

	/* ZG-228Z melody service */
	ZG228Z_PS_H,
	ZG228Z_Play_CD_H,
	ZG228Z_Play_DP_H,
	ZG228Z_Slot1_CD_H,
	ZG228Z_Slot1_DP_H,
	ZG228Z_Slot2_CD_H,
	ZG228Z_Slot2_DP_H,
	ZG228Z_Slot3_CD_H,
	ZG228Z_Slot3_DP_H,

	ATT_END_H
} ATT_HANDLE;

extern u8 mac_public[6];

void user_ble_init(bool isRetention);
void app_ble_enterBleOnly(void);	/* TX pad bridged: BLE mode + adv + LED blink */
void app_ble_task(void);	/* BLE-only mode: check the bridge, reboot when removed */

#endif /* SRC_BLE_CFG_H_ */
