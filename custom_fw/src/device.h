#ifndef _DEVICE_H_
#define _DEVICE_H_

#include "zcl_include.h"
#include "zdo_api.h"
#include "app_cfg.h"

/**********************************************************************
 * CONSTANTS
 */
/**********************************************************************
 * FUNCTIONS implemented in zb_appCb.c
 */
status_t zg228z_basicCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload);
status_t zg228z_identifyCb(zclIncomingAddrInfo_t *pAddrInfo, u8 cmdId, void *cmdPayload);
void zg228z_zclHook(zclIncoming_t *pInHdlrMsg);
void zg228z_leaveIndHandler(nlme_leave_ind_t *pLeaveInd);
void zg228z_leaveCnfHandler(nlme_leave_cnf_t *pLeaveCnf);

/**********************************************************************
 * TYPEDEFS
 */
typedef struct{
	u8 keyType; /* SS_UNIQUE_LINK_KEY or SS_GLOBAL_LINK_KEY */
	u8 key[16]; /* the key used */
}app_linkKey_info_t;

/*
 * Application context
 */
typedef struct{
	ev_timer_event_t *timerLedEvt;
	ev_timer_event_t *timerKeyEvt;
	ev_timer_event_t *timerIdentifyEvt;
#if REJOIN_FAILURE_TIMER
	ev_timer_event_t *timerRejoinBackoffEvt;
#endif
	ev_timer_event_t *timerSteerEvt;

	u32 secTimeTik;		/* second tick reference */
	u32 utc_sec;		/* uptime, seconds */

	u32 keyPressedTime;

	/* LED blink state */
	u16 ledOnTime;
	u16 ledOffTime;
	u8  oriSta;
	u8  sta;
	u8  times;

	u8  keyPressed;		/* button is currently down */

	/* battery */
	u16 battery_mv;
	u8  battery_level;	/* 0..100 % */

	/* Tuya report destination, captured from the last incoming command */
	u16 tuyaDstAddr;	/* 0xffff = none yet, use coordinator default */
	u8  tuyaDstEp;

	/* BLE-only mode (TX pad PB1 bridged to RX pad PB7) */
	u8  bleOnly;

	app_linkKey_info_t tcLinkKey;
} app_ctx_t;

/**
 *  @brief Defined for basic cluster attributes
 */
typedef struct{
	u8 	zclVersion;
	u8	appVersion;
	u8	stackVersion;
	u8	hwVersion;
	u8	manuName[ZCL_BASIC_MAX_LENGTH];
	u8	modelId[ZCL_BASIC_MAX_LENGTH];
	u8	swBuildId[ZCL_BASIC_MAX_LENGTH];
	u8 	dateCode[ZCL_BASIC_MAX_LENGTH];
	u8	powerSource;
	u8	deviceEnable;
}zcl_basicAttr_t;

/**
 *  @brief Defined for identify cluster attributes
 */
typedef struct{
	u16	identifyTime;
}zcl_identifyAttr_t;

/**
 *  @brief Defined for power configuration cluster attributes
 */
typedef struct{
	u8  batteryVoltage;      /* 0x0020, 100 mV units */
	u8  batteryPercentage;   /* 0x0021, 0.5 % units */
}zcl_powerAttr_t;

/**********************************************************************
 * GLOBAL VARIABLES
 */
extern app_ctx_t g_appCtx;

extern bdb_appCb_t g_zbDemoBdbCb;
extern bdb_commissionSetting_t g_bdbCommissionSetting;

extern const af_simple_descriptor_t zg228z_simpleDesc;
extern const zcl_specClusterInfo_t zg228z_clusterList[];
extern u8 zg228z_clusterNum;

/* Attributes */
extern zcl_basicAttr_t g_zcl_basicAttrs;
extern zcl_identifyAttr_t g_zcl_identifyAttrs;
extern zcl_powerAttr_t g_zcl_powerAttrs;

/**********************************************************************
 * FUNCTIONS
 */
void user_zb_init(bool isRetention);
void user_app_init(void);
void stack_init(void);
void app_task(void);


#endif /* _DEVICE_H_ */
