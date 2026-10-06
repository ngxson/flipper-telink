/**********************************************************************
 * Zigbee application callbacks: BDB commissioning, ZDO leave,
 * identify, and the ZCL hook used to learn the gateway address.
 **********************************************************************/
#include "tl_common.h"
#include "device.h"
#include "app.h"
#include "app_cfg.h"
#include "app_ui.h"
#include "zb_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "tuya_cluster.h"

/**********************************************************************
 * LOCAL FUNCTIONS / PROTOTYPES
 */
void zg228z_bdbInitCb(u8 status, u8 joinedNetwork);
void zg228z_bdbCommissioningCb(u8 status, void *arg);
void zg228z_bdbIdentifyCb(u8 endpoint, u16 srcAddr, u16 identifyTime);
static s32 zg228z_identifyTimerCb(void *arg);

/**********************************************************************
 * GLOBAL VARIABLES
 */
bdb_appCb_t g_zbDemoBdbCb =
{
	zg228z_bdbInitCb,
	zg228z_bdbCommissioningCb,
	zg228z_bdbIdentifyCb,
	NULL
};

/**********************************************************************
 * Commissioning timers
 */
static s32 zg228z_bdbNetworkSteerStart(void *arg)
{
	bdb_networkSteerStart();

	g_appCtx.timerSteerEvt = NULL;
	return -1;
}

#if REJOIN_FAILURE_TIMER
static s32 zg228z_rejoinBackoff(void *arg)
{
	if(zb_isDeviceFactoryNew()){
		g_appCtx.timerRejoinBackoffEvt = NULL;
		return -1;
	}

	zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
	return 0;
}
#endif

/**********************************************************************
 * BDB callbacks
 */
void zg228z_bdbInitCb(u8 status, u8 joinedNetwork)
{
	if(status == BDB_INIT_STATUS_SUCCESS){
		if(joinedNetwork){
			zb_setPollRate(DEFAULT_POLL_RATE);
		}else{
			u16 jitter = 0;
			do{
				jitter = zb_random() % 0x0fff;
			}while(jitter == 0);

			if(g_appCtx.timerSteerEvt){
				TL_ZB_TIMER_CANCEL(&g_appCtx.timerSteerEvt);
			}
			g_appCtx.timerSteerEvt = TL_ZB_TIMER_SCHEDULE(zg228z_bdbNetworkSteerStart, NULL, jitter);
		}
	}
#if REJOIN_FAILURE_TIMER
	else{
		if(joinedNetwork){
			zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
		}
	}
#endif
}

void zg228z_bdbCommissioningCb(u8 status, void *arg)
{
	switch(status){
		case BDB_COMMISSION_STA_SUCCESS:
			led_blink_start(3, 250, 250);
			zb_setPollRate(DEFAULT_POLL_RATE);

			if(g_appCtx.timerSteerEvt){
				TL_ZB_TIMER_CANCEL(&g_appCtx.timerSteerEvt);
			}
#if REJOIN_FAILURE_TIMER
			if(g_appCtx.timerRejoinBackoffEvt){
				TL_ZB_TIMER_CANCEL(&g_appCtx.timerRejoinBackoffEvt);
			}
#endif
			/* announce ourselves to the gateway */
			tuya_reportAll();
			break;
		case BDB_COMMISSION_STA_IN_PROGRESS:
			break;
		case BDB_COMMISSION_STA_NO_NETWORK:
		case BDB_COMMISSION_STA_TCLK_EX_FAILURE:
		case BDB_COMMISSION_STA_TARGET_FAILURE:
			{
				u16 jitter = 0;
				do{
					jitter = zb_random() % 0x0fff;
				}while(jitter == 0);

				if(g_appCtx.timerSteerEvt){
					TL_ZB_TIMER_CANCEL(&g_appCtx.timerSteerEvt);
				}
#if REJOIN_FAILURE_TIMER
				g_appCtx.timerSteerEvt = TL_ZB_TIMER_SCHEDULE(zg228z_bdbNetworkSteerStart, NULL, jitter + 60000);
#else
				g_appCtx.timerSteerEvt = TL_ZB_TIMER_SCHEDULE(zg228z_bdbNetworkSteerStart, NULL, jitter);
#endif
			}
			break;
		case BDB_COMMISSION_STA_NO_SCAN_RESPONSE:
		case BDB_COMMISSION_STA_PARENT_LOST:
#if REJOIN_FAILURE_TIMER
			zg228z_rejoinBackoff(NULL);
#else
			zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
#endif
			break;
		case BDB_COMMISSION_STA_REJOIN_FAILURE:
			if(!zb_isDeviceFactoryNew()){
#if REJOIN_FAILURE_TIMER
				g_appCtx.timerRejoinBackoffEvt = TL_ZB_TIMER_SCHEDULE(zg228z_rejoinBackoff, NULL, 360 * 1000);
#else
				zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
#endif
			}
			break;
		default:
			break;
	}
}

/**********************************************************************
 * Identify (blink the LED for the identify time)
 */
static s32 zg228z_identifyTimerCb(void *arg)
{
	g_appCtx.timerIdentifyEvt = NULL;
	g_zcl_identifyAttrs.identifyTime = 0;
	led_blink_stop();
	return -1;
}

static void zg228z_identifyCmd(u16 identifyTime)
{
	if(g_appCtx.timerIdentifyEvt){
		TL_ZB_TIMER_CANCEL(&g_appCtx.timerIdentifyEvt);
	}

	if(identifyTime){
		g_zcl_identifyAttrs.identifyTime = identifyTime;
		led_blink_start(0, 250, 250);
		g_appCtx.timerIdentifyEvt = TL_ZB_TIMER_SCHEDULE(zg228z_identifyTimerCb, NULL, (u32)identifyTime * 1000);
	}else{
		zg228z_identifyTimerCb(NULL);
	}
}

void zg228z_bdbIdentifyCb(u8 endpoint, u16 srcAddr, u16 identifyTime)
{
	zg228z_identifyCmd(identifyTime);
}

status_t zg228z_identifyCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload)
{
	switch(cmdId){
		case ZCL_CMD_IDENTIFY:
			zg228z_identifyCmd(*(u16 *)cmdPayload);
			break;
		case ZCL_CMD_IDENTIFY_QUERY:
			break;
		default:
			break;
	}
	return ZCL_STA_SUCCESS;
}

/**********************************************************************
 * Basic cluster callback: remember who is talking to us (Z2M reads the
 * basic attributes right after pairing) so our Tuya data reports reach
 * the gateway endpoint even before it has sent us a Tuya command.
 */
status_t zg228z_basicCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload)
{
	if(pAddrInfo->srcAddr != 0xffff && pAddrInfo->srcAddr != 0x0000){
		g_appCtx.tuyaDstAddr = pAddrInfo->srcAddr;
		g_appCtx.tuyaDstEp = pAddrInfo->srcEp;
	}else if(pAddrInfo->srcAddr != 0xffff){
		g_appCtx.tuyaDstEp = pAddrInfo->srcEp;
	}
	return ZCL_STA_SUCCESS;
}

/**********************************************************************
 * ZCL hook (called for parsed foundation commands)
 */
void zg228z_zclHook(zclIncoming_t *pInHdlrMsg)
{
	if(pInHdlrMsg->addrInfo.srcAddr != 0xffff){
		if(pInHdlrMsg->addrInfo.srcAddr != 0x0000){
			g_appCtx.tuyaDstAddr = pInHdlrMsg->addrInfo.srcAddr;
		}
		g_appCtx.tuyaDstEp = pInHdlrMsg->addrInfo.srcEp;
	}
}

/**********************************************************************
 * ZDO leave handlers
 */
void zg228z_leaveCnfHandler(nlme_leave_cnf_t *pLeaveCnf)
{
	if(pLeaveCnf->status == SUCCESS){
		SYSTEM_RESET();
	}
}

void zg228z_leaveIndHandler(nlme_leave_ind_t *pLeaveInd)
{
}
