/**********************************************************************
 * Tuya manufacturer-specific cluster (0xEF00) for the ZG-228Z.
 *
 * Payload format (zigbee-herdsman "manuSpecificTuya", LIST_TUYA_DATAPOINT_VALUES):
 *   seq(u16, ZCL LE) | dp(u8) | datatype(u8) | len(u16 BE) | data[len]
 * Multiple dp values may follow each other in one command.
 * The commands are cluster-specific ZCL commands with this payload
 * (dataQuery has an empty payload).
 **********************************************************************/
#include "tl_common.h"
#include "zcl_include.h"
#include "tuya_cluster.h"
#include "device.h"
#include "app.h"

#define TUYA_MAX_DPS_PER_MSG		10

typedef struct{
	u8 dp;
	u8 type;
	u16 len;
	u8 *data;
} tuyaDpValue_t;

static status_t tuya_cmdHandler(zclIncoming_t *pInMsg);

/* ZCL registration entry point, matching the SDK's cluster_registerFunc_t */
status_t zcl_tuya_register(u8 endpoint, u16 manuCode, u8 attrNum, const zclAttrInfo_t attrTbl[], cluster_forAppCb_t cb)
{
	return zcl_registerCluster(endpoint, ZCL_CLUSTER_TUYA, manuCode, attrNum, attrTbl, tuya_cmdHandler, cb);
}

/**********************************************************************
 * Sending
 */

/* Remember where the last gateway message came from; fall back to the
 * coordinator (short address 0, endpoint 1) before anything arrives. */
static void tuya_fillDst(epInfo_t *pDstEpInfo)
{
	memset((u8 *)pDstEpInfo, 0, sizeof(epInfo_t));
	pDstEpInfo->dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	pDstEpInfo->dstAddr.shortAddr = 0x0000;
	pDstEpInfo->dstEp = 1;
	pDstEpInfo->profileId = HA_PROFILE_ID;
	pDstEpInfo->txOptions |= APS_TX_OPT_ACK_TX;

	if(g_appCtx.tuyaDstAddr != 0xffff){
		pDstEpInfo->dstAddr.shortAddr = g_appCtx.tuyaDstAddr;
		pDstEpInfo->dstEp = g_appCtx.tuyaDstEp;
	}
}

/* Append one dp value to the payload; returns the number of bytes written */
static u16 tuya_appendDp(u8 *p, u16 offset, u8 dp, u8 type, u16 len, u8 *data)
{
	u16 i;
	p[offset++] = dp;
	p[offset++] = type;
	p[offset++] = (len >> 8) & 0xff;
	p[offset++] = len & 0xff;
	for(i = 0; i < len; i++){
		p[offset++] = data[i];
	}
	return offset;
}

/* Send a Tuya command carrying seq + one dp value */
static status_t tuya_sendOne(u8 cmd, u16 seq, u8 dp, u8 type, u16 len, u8 *data)
{
	epInfo_t dstEpInfo;
	u8 buf[16];

	u16 off = 0;
	buf[off++] = seq & 0xff;
	buf[off++] = (seq >> 8) & 0xff;
	off = tuya_appendDp(buf, off, dp, type, len, data);

	tuya_fillDst(&dstEpInfo);
	return zcl_sendCmd(ZG228Z_ENDPOINT, &dstEpInfo, ZCL_CLUSTER_TUYA, cmd, TRUE,
					   ZCL_FRAME_SERVER_CLIENT_DIR, TRUE, MANUFACTURER_CODE_NONE, ZCL_SEQ_NUM, off, buf);
}

status_t tuya_reportDpEnum(u8 dp, u8 val)
{
	return tuya_sendOne(ZCL_CMD_TUYA_DATA_REPORT, 0, dp, TUYA_TYPE_ENUM, 1, &val);
}

status_t tuya_reportDpBool(u8 dp, u8 val)
{
	return tuya_sendOne(ZCL_CMD_TUYA_DATA_REPORT, 0, dp, TUYA_TYPE_BOOL, 1, &val);
}

status_t tuya_reportDpValue(u8 dp, u32 val)
{
	u8 data[4];
	data[0] = (val >> 24) & 0xff;
	data[1] = (val >> 16) & 0xff;
	data[2] = (val >> 8) & 0xff;
	data[3] = val & 0xff;
	return tuya_sendOne(ZCL_CMD_TUYA_DATA_REPORT, 0, dp, TUYA_TYPE_VALUE, 4, data);
}

void tuya_reportAll(void)
{
	app_tuyaReportAll();
}

/**********************************************************************
 * Receiving
 */

static status_t tuya_cmdHandler(zclIncoming_t *pInMsg)
{
	u16 seq;

	if(pInMsg->hdr.frmCtrl.bf.dir != ZCL_FRAME_CLIENT_SERVER_DIR){
		/* we only implement the server side */
		return ZCL_STA_UNSUP_CLUSTER_COMMAND;
	}

	/* remember the gateway address for our reports */
	g_appCtx.tuyaDstAddr = pInMsg->addrInfo.srcAddr;
	g_appCtx.tuyaDstEp = pInMsg->addrInfo.srcEp;

	switch(pInMsg->hdr.cmd){
		case ZCL_CMD_TUYA_DATA_REQUEST:
			break;
		case ZCL_CMD_TUYA_DATA_QUERY:
			tuya_reportAll();
			return ZCL_STA_SUCCESS;
		default:
			return ZCL_STA_UNSUP_CLUSTER_COMMAND;
	}

	/* data request: seq(u16) dp(u8) type(u8) len(u16 BE) data ... */
	if(pInMsg->dataLen < 6){
		return ZCL_STA_MALFORMED_COMMAND;
	}

	u8 *p = pInMsg->pData;
	u16 remain = pInMsg->dataLen;

	seq = p[0] | (p[1] << 8);

	/* pass every dp value to the application; remember what was accepted so
	 * the response echoes the same values */
	u8 rsp[80];
	u16 rspOff = 0;
	u8 accepted = 0;

	rsp[rspOff++] = seq & 0xff;
	rsp[rspOff++] = (seq >> 8) & 0xff;

	remain -= 2;
	p += 2;

	while(remain >= 4){
		u8 dp = p[0];
		u8 type = p[1];
		u16 len = ((u16)p[2] << 8) | p[3];
		u8 *data = p + 4;

		if((remain - 4) < len){
			break;	/* malformed length */
		}

		if(app_tuyaSetDp(dp, type, len, data) && (rspOff + 6 + len) <= sizeof(rsp)){
			rspOff = tuya_appendDp(rsp, rspOff, dp, type, len, data);
			accepted++;
		}

		p += 4 + len;
		remain -= 4 + len;
	}

	if(accepted){
		epInfo_t dstEpInfo;
		tuya_fillDst(&dstEpInfo);
		zcl_sendCmd(ZG228Z_ENDPOINT, &dstEpInfo, ZCL_CLUSTER_TUYA, ZCL_CMD_TUYA_DATA_RESPONSE, TRUE,
					ZCL_FRAME_SERVER_CLIENT_DIR, TRUE, MANUFACTURER_CODE_NONE, ZCL_SEQ_NUM, rspOff, rsp);
	}

	return ZCL_STA_SUCCESS;
}
