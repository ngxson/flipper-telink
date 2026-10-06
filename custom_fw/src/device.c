/**********************************************************************
 * ZG-228Z (HOBEIAN vibration alarm, TLSR8258) - endpoint / stack init.
 *
 * Written fresh for this project, structure follows the Telink Zigbee SDK
 * sample apps (and pvvx/BZdevice's adaptation of them).
 **********************************************************************/
#include "tl_common.h"
#include "device.h"
#include "zb_api.h"
#include "zdo_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "app_cfg.h"
#include "app.h"
#include "tuya_cluster.h"

/**********************************************************************
 * GLOBAL VARIABLES
 */
app_ctx_t g_appCtx;

/*Must declare the application call back function which used by ZDO layer*/
const zdo_appIndCb_t appCbLst = {
	bdb_zdoStartDevCnf,		/* start device cnf cb */
	NULL,					/* reset cnf cb */
	NULL,					/* device announce indication cb */
	zg228z_leaveIndHandler,	/* leave ind cb */
	zg228z_leaveCnfHandler,	/* leave cnf cb */
	NULL,					/* nwk update ind cb */
	NULL,					/* permit join ind cb */
	NULL,					/* nlme sync cnf cb */
	NULL,					/* tc join ind cb */
	NULL,					/* tc detects that the frame counter is near limit */
};

/**
 *  @brief Definition for bdb commissioning setting
 */
bdb_commissionSetting_t g_bdbCommissionSetting = {
	.linkKey.tcLinkKey.keyType = SS_GLOBAL_LINK_KEY,
	.linkKey.tcLinkKey.key = (u8 *)tcLinkKeyCentralDefault,			/* can use unique link key stored in NV */

	.linkKey.distributeLinkKey.keyType = MASTER_KEY,
	.linkKey.distributeLinkKey.key = (u8 *)linkKeyDistributedMaster,	/* use linkKeyDistributedCertification before testing */

	.linkKey.touchLinkKey.keyType = MASTER_KEY,
	.linkKey.touchLinkKey.key = (u8 *)touchLinkKeyMaster,				/* use touchLinkKeyCertification before testing */

#if TOUCHLINK_SUPPORT
	.touchlinkEnable = 1,												/* enable touch-link */
#else
	.touchlinkEnable = 0,												/* disable touch-link */
#endif
	.touchlinkChannel = DEFAULT_CHANNEL, 								/* touch-link default operation channel for target */
	.touchlinkLqiThreshold = 0xA0,			   							/* threshold for touch-link scan req/resp command */
};

/**********************************************************************
 * ZCL endpoint definition
 */
const u16 zg228z_inClusterList[] =
{
	ZCL_CLUSTER_GEN_BASIC,
	ZCL_CLUSTER_GEN_POWER_CFG,
	ZCL_CLUSTER_GEN_IDENTIFY,
	ZCL_CLUSTER_TUYA,
};

const u16 zg228z_outClusterList[] =
{
};

#define ZG228Z_IN_CLUSTER_NUM	(sizeof(zg228z_inClusterList) / sizeof(zg228z_inClusterList[0]))
#define ZG228Z_OUT_CLUSTER_NUM	(sizeof(zg228z_outClusterList) / sizeof(zg228z_outClusterList[0]))

const af_simple_descriptor_t zg228z_simpleDesc =
{
	HA_PROFILE_ID,		/* Application profile identifier */
	ZG228Z_DEVICE_ID,	/* HA "Simple Sensor" device id 0x0005 */
	ZG228Z_ENDPOINT,	/* Endpoint */
	1,					/* Application device version */
	0,					/* Reserved */
	ZG228Z_IN_CLUSTER_NUM,		/* Application input cluster count */
	ZG228Z_OUT_CLUSTER_NUM,		/* Application output cluster count */
	(u16 *)zg228z_inClusterList,	/* Application input cluster list */
	(u16 *)zg228z_outClusterList,	/* Application output cluster list */
};

/* Basic */
zcl_basicAttr_t g_zcl_basicAttrs =
{
	.zclVersion 	= 0x03,
	.appVersion 	= APP_RELEASE,
	.stackVersion 	= STACK_RELEASE,
	.hwVersion		= 0x01,
	.manuName		= {7,'H','O','B','E','I','A','N'},
	.modelId		= {7,'Z','G','-','2','2','8','Z'},
	.swBuildId		= {1,'1'},
	.dateCode		= {8,'2','0','2','6','0','1','0','1'},
	.powerSource	= POWER_SOURCE_BATTERY,
	.deviceEnable	= TRUE,
};

const zclAttrInfo_t basic_attrTbl[] =
{
	{ ZCL_ATTRID_BASIC_ZCL_VER,      	ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,  							(u8*)&g_zcl_basicAttrs.zclVersion},
	{ ZCL_ATTRID_BASIC_APP_VER,      	ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,  							(u8*)&g_zcl_basicAttrs.appVersion},
	{ ZCL_ATTRID_BASIC_STACK_VER,    	ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,  							(u8*)&g_zcl_basicAttrs.stackVersion},
	{ ZCL_ATTRID_BASIC_HW_VER,       	ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ,  							(u8*)&g_zcl_basicAttrs.hwVersion},
	{ ZCL_ATTRID_BASIC_MFR_NAME,         	ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,  							(u8*)g_zcl_basicAttrs.manuName},
	{ ZCL_ATTRID_BASIC_MODEL_ID,     	ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,							(u8*)g_zcl_basicAttrs.modelId},
	{ ZCL_ATTRID_BASIC_DATE_CODE,        	ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,                        	(u8*)g_zcl_basicAttrs.dateCode},
	{ ZCL_ATTRID_BASIC_POWER_SOURCE, 		ZCL_DATA_TYPE_ENUM8,    ACCESS_CONTROL_READ,  							(u8*)&g_zcl_basicAttrs.powerSource},
	{ ZCL_ATTRID_BASIC_DEV_ENABLED,  		ZCL_DATA_TYPE_BOOLEAN,  ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, 	(u8*)&g_zcl_basicAttrs.deviceEnable},
	{ ZCL_ATTRID_BASIC_SW_BUILD_ID,  		ZCL_DATA_TYPE_CHAR_STR, ACCESS_CONTROL_READ,  							(u8*)g_zcl_basicAttrs.swBuildId},

	{ ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, 	ZCL_DATA_TYPE_UINT16,  	ACCESS_CONTROL_READ,  						(u8*)&zcl_attr_global_clusterRevision},
};

#define ZCL_BASIC_ATTR_NUM 		sizeof(basic_attrTbl) / sizeof(zclAttrInfo_t)

/* Identify */
zcl_identifyAttr_t g_zcl_identifyAttrs =
{
	.identifyTime = 0x0000,
};

const zclAttrInfo_t identify_attrTbl[] =
{
	{ ZCL_ATTRID_IDENTIFY_TIME,  			ZCL_DATA_TYPE_UINT16,   ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE, (u8*)&g_zcl_identifyAttrs.identifyTime },
	{ ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, 	ZCL_DATA_TYPE_UINT16,  	ACCESS_CONTROL_READ,  						(u8*)&zcl_attr_global_clusterRevision},
};

#define ZCL_IDENTIFY_ATTR_NUM	sizeof(identify_attrTbl) / sizeof(zclAttrInfo_t)

/* Power configuration */
zcl_powerAttr_t g_zcl_powerAttrs =
{
	.batteryVoltage    = 0xff,	/* in 100 mV units, 0xff - unknown */
	.batteryPercentage = 0xff,	/* in 0.5% units, 0xff - unknown */
};

const zclAttrInfo_t powerCfg_attrTbl[] =
{
	{ ZCL_ATTRID_BATTERY_VOLTAGE,      		   ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE,	(u8*)&g_zcl_powerAttrs.batteryVoltage},
	{ ZCL_ATTRID_BATTERY_PERCENTAGE_REMAINING, ZCL_DATA_TYPE_UINT8,    ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE, (u8*)&g_zcl_powerAttrs.batteryPercentage},
	{ ZCL_ATTRID_GLOBAL_CLUSTER_REVISION, 	  ZCL_DATA_TYPE_UINT16,  	ACCESS_CONTROL_READ,  							(u8*)&zcl_attr_global_clusterRevision},
};

#define	ZCL_POWER_CFG_ATTR_NUM	sizeof(powerCfg_attrTbl) / sizeof(zclAttrInfo_t)

const zcl_specClusterInfo_t zg228z_clusterList[] =
{
	{ZCL_CLUSTER_GEN_BASIC,		MANUFACTURER_CODE_NONE, ZCL_BASIC_ATTR_NUM,		basic_attrTbl,		zcl_basic_register,		zg228z_basicCb},
	{ZCL_CLUSTER_GEN_POWER_CFG,	MANUFACTURER_CODE_NONE, ZCL_POWER_CFG_ATTR_NUM,	powerCfg_attrTbl,	zcl_powerCfg_register,	NULL},
	{ZCL_CLUSTER_GEN_IDENTIFY,	MANUFACTURER_CODE_NONE, ZCL_IDENTIFY_ATTR_NUM,	identify_attrTbl,	zcl_identify_register,	zg228z_identifyCb},
	{ZCL_CLUSTER_TUYA,			MANUFACTURER_CODE_NONE, 0,						NULL,				zcl_tuya_register,		NULL},
};

u8 zg228z_clusterNum = sizeof(zg228z_clusterList) / sizeof(zg228z_clusterList[0]);

/**********************************************************************
 * FUNCTIONS
 */

void stack_init(void)
{
	/* Initialize ZB stack */
	zb_init();

	/* Register stack CB */
	zb_zdoCbRegister((zdo_appIndCb_t *)&appCbLst);
}

void user_app_init(void)
{
	af_powerDescPowerModeUpdate(POWER_MODE_RECEIVER_COMES_WHEN_STIMULATED);

	/* Initialize ZCL layer, register the incoming message hook */
	zcl_init(zg228z_zclHook);

	/* Register endPoint */
	af_endpointRegister(ZG228Z_ENDPOINT, (af_simple_descriptor_t *)&zg228z_simpleDesc, zcl_rx_handler, NULL);

	zcl_reportingTabInit();

	/* Register ZCL specific cluster information */
	zcl_register(ZG228Z_ENDPOINT, zg228z_clusterNum, (zcl_specClusterInfo_t *)zg228z_clusterList);

	/* Application */
	app_init();
}

void user_zb_init(bool isRetention)
{
#if PA_ENABLE
	rf_paInit(PA_TX, PA_RX);
#endif

	if(!isRetention){
		/* Initialize Stack */
		stack_init();

		/* Initialize user application */
		user_app_init();

		/* Load the pre-install code from flash */
		if(bdb_preInstallCodeLoad(&g_appCtx.tcLinkKey.keyType, g_appCtx.tcLinkKey.key) == RET_OK){
			g_bdbCommissionSetting.linkKey.tcLinkKey.keyType = g_appCtx.tcLinkKey.keyType;
			g_bdbCommissionSetting.linkKey.tcLinkKey.key = g_appCtx.tcLinkKey.key;
		}

		/* Initialize BDB */
		u8 repower = drv_pm_deepSleep_flag_get() ? 0 : 1;
		bdb_init((af_simple_descriptor_t *)&zg228z_simpleDesc, &g_bdbCommissionSetting, &g_zbDemoBdbCb, repower);
	}else{
		/* Re-config phy when system recovery from deep sleep with retention */
		mac_phyReconfig();
	}
}
