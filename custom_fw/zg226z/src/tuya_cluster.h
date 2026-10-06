#ifndef _TUYA_CLUSTER_H_
#define _TUYA_CLUSTER_H_

#include "tl_common.h"
#include "zcl_include.h"

/* Tuya manufacturer-specific cluster */
#define ZCL_CLUSTER_TUYA					0xEF00

/* Cluster commands (Z2M "manuSpecificTuya" definition) */
#define ZCL_CMD_TUYA_DATA_REQUEST			0x00	/* gateway -> device: set DP(s) */
#define ZCL_CMD_TUYA_DATA_RESPONSE			0x01	/* device -> gateway: ack a data request */
#define ZCL_CMD_TUYA_DATA_REPORT				0x02	/* device -> gateway: unsolicited report */
#define ZCL_CMD_TUYA_DATA_QUERY				0x03	/* gateway -> device: report everything */

/* Tuya DP data types */
#define TUYA_TYPE_RAW						0x00
#define TUYA_TYPE_BOOL						0x01
#define TUYA_TYPE_VALUE						0x02	/* 4-byte big endian */
#define TUYA_TYPE_STRING					0x03
#define TUYA_TYPE_ENUM						0x04
#define TUYA_TYPE_BITMAP					0x05

/* ZG-228Z datapoints (see zigbee-herdsman-converters src/devices/hobeian.ts) */
#define TUYA_DP_VIBRATION					1		/* enum: 1 = vibration, 0 = clear */
#define TUYA_DP_BATTERY						4		/* value: 0..100 % */
#define TUYA_DP_SENSITIVITY				6		/* value: 1..50 */
#define TUYA_DP_VIB_SIREN					101		/* enum: 0 = OFF, 1 = ON */
#define TUYA_DP_MUFFLING					102		/* bool: 1 = stop the alarm */
#define TUYA_DP_ALARM_VOLUME				103		/* enum: 0..2 = melody slot 1..3, 3 = mute */
#define TUYA_DP_ALARM_RING					104		/* enum: 0 mute, 1 beep, 2 music */
#define TUYA_DP_ALARM						105		/* enum: 0 beep, 1 ring, 2 stop */
#define TUYA_DP_ALARM_TIME					106		/* value: 0..1800 s */

/* Cluster registration (same signature as the SDK's zcl_xxx_register helpers) */
status_t zcl_tuya_register(u8 endpoint, u16 manuCode, u8 attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb);

/* Send a single DP as an unsolicited "data report" (cmd 0x02) */
status_t tuya_reportDpEnum(u8 dp, u8 val);
status_t tuya_reportDpBool(u8 dp, u8 val);
status_t tuya_reportDpValue(u8 dp, u32 val);

/* Report every DP as one "data report" (answer to data query) */
void tuya_reportAll(void);

/* App callback: a DP value was received from the gateway. Returns TRUE if the
 * DP is known and accepted. */
bool app_tuyaSetDp(u8 dp, u8 type, u16 len, u8 *data);

#endif /* _TUYA_CLUSTER_H_ */
