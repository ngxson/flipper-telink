/**********************************************************************
 * Zigbee mode (normal operation): a sleepy end device that Zigbee2MQTT
 * takes for the stock Tuya remote, using the stock TS0044 / TS0046
 * definitions unchanged.
 *
 * Identity (what Z2M matches on):
 *   modelId           "TS0044" (4 buttons) or "TS0046" (6 buttons), from
 *                     ch_num in the Tuya factory config (buttons.c)
 *   manufacturerName  "_TZ3000_" + the Tuya product id at 0xFB000, exactly
 *                     like the stock firmware builds it (SS6400zb unit:
 *                     _TZ3000_ee8nrt2l = Z2M "LoraTap SS6400ZB"; TS0046
 *                     unit: _TZ3000_iszegwpd); stock default
 *                     _TZ3000_eqzt4y9t if that sector is empty
 *   powerSource       battery, manufacturer code 0x1141 (Telink, as stock)
 *
 * Endpoints 1..N, one per button, each with genOnOff (0x0006) in; ep 1
 * also has genBasic and genPowerCfg. A press goes out FROM the button's
 * endpoint to the coordinator as the Tuya scene command that Z2M's
 * tuya.fz.on_off_action decodes:
 *   genOnOff, cluster specific, client->server, cmd 0xFD, payload u8
 *   0 = single (short press), 2 = hold (1 s)  -> "<ep>_single" / "<ep>_hold"
 * Double (1) is never sent: no 300 ms double-click window in this
 * firmware, a short press goes out on the release edge.
 * Battery: genPowerCfg batteryVoltage (0x20, 100 mV) and
 * batteryPercentageRemaining (0x21, 0.5 %), readable and reported every
 * BATTERY_REPORT_PERIOD_S (the TS0044 definition reads both and
 * configures no reporting).
 *
 * Pairing: a factory-new device starts network steering at power-up;
 * holding button 1 for 10 s (LED blinks fast, release to confirm) leaves
 * the network, resets to factory new and steers again. Steering gives up
 * after ZB_STEER_ATTEMPTS tries so an unpaired remote doesn't scan its
 * coin cell flat.
 *
 * Power: deep sleep with RAM retention whenever the stack is idle, woken
 * by the poll / battery timers or any button press; the CPU stays awake
 * while a button is held or the LED is on.
 **********************************************************************/
#include "tl_common.h"
#include "zb_api.h"
#include "zdo_api.h"
#include "zcl_include.h"
#include "bdb.h"
#include "zigbee_ble_switch.h"
#include "buttons.h"
#include "log.h"
#include "zb_dev.h"

#define ZB_EP_MAX				6

/* Tuya: product id of this unit, and the stock default manufacturer name */
#define TUYA_PID_ADDR			0xFB000
#define TUYA_PID_LEN			8
#define TUYA_MANU_PREFIX		"_TZ3000_"
#define TUYA_PID_DEFAULT		"eqzt4y9t"

/* Tuya scene switch command on genOnOff (Z2M "tuyaAction") */
#define ZCL_CMD_TUYA_ACTION		0xFD
#define TUYA_ACTION_SINGLE		0
#define TUYA_ACTION_HOLD		2

extern void led_blip(void);			/* main.c */
extern u8 led_on;					/* main.c: blip state */
extern u32 led_tick;

extern void adc_channel_init(ADC_InputPchTypeDef p_ain);	/* patch_sdk/adc_drv.c */
extern u16 get_adc_mv(int flg);

extern void rf_rx_irq_handler(void);
extern void rf_tx_irq_handler(void);

/* mac_phy.c asks who owns the radio: always Zigbee in this mode */
app_dualModeInfo_t g_dualModeInfo = {
	.slot = DUALMODE_SLOT_ZIGBEE,
};

/**********************************************************************
 * ZCL attributes
 */
typedef struct{
	u8	zclVersion;
	u8	appVersion;
	u8	stackVersion;
	u8	hwVersion;
	u8	manuName[ZCL_BASIC_MAX_LENGTH];
	u8	modelId[ZCL_BASIC_MAX_LENGTH];
	u8	swBuildId[ZCL_BASIC_MAX_LENGTH];
	u8	dateCode[ZCL_BASIC_MAX_LENGTH];
	u8	powerSource;
	u8	deviceEnable;
}zb_basicAttr_t;

static zb_basicAttr_t g_basicAttrs = {
	.zclVersion		= 0x03,
	.appVersion		= APP_BUILD,
	.stackVersion	= STACK_RELEASE,
	.hwVersion		= 0x01,
	.manuName		= {0},		/* zb_identityInit */
	.modelId		= {0},
	.swBuildId		= {0},
	.dateCode		= {8,'2','0','2','6','1','0','0','8'},
	.powerSource	= POWER_SOURCE_BATTERY,
	.deviceEnable	= TRUE,
};

static const zclAttrInfo_t basic_attrTbl[] =
{
	{ ZCL_ATTRID_BASIC_ZCL_VER,				ZCL_DATA_TYPE_UINT8,	ACCESS_CONTROL_READ,	(u8*)&g_basicAttrs.zclVersion},
	{ ZCL_ATTRID_BASIC_APP_VER,				ZCL_DATA_TYPE_UINT8,	ACCESS_CONTROL_READ,	(u8*)&g_basicAttrs.appVersion},
	{ ZCL_ATTRID_BASIC_STACK_VER,			ZCL_DATA_TYPE_UINT8,	ACCESS_CONTROL_READ,	(u8*)&g_basicAttrs.stackVersion},
	{ ZCL_ATTRID_BASIC_HW_VER,				ZCL_DATA_TYPE_UINT8,	ACCESS_CONTROL_READ,	(u8*)&g_basicAttrs.hwVersion},
	{ ZCL_ATTRID_BASIC_MFR_NAME,			ZCL_DATA_TYPE_CHAR_STR,	ACCESS_CONTROL_READ,	(u8*)g_basicAttrs.manuName},
	{ ZCL_ATTRID_BASIC_MODEL_ID,			ZCL_DATA_TYPE_CHAR_STR,	ACCESS_CONTROL_READ,	(u8*)g_basicAttrs.modelId},
	{ ZCL_ATTRID_BASIC_DATE_CODE,			ZCL_DATA_TYPE_CHAR_STR,	ACCESS_CONTROL_READ,	(u8*)g_basicAttrs.dateCode},
	{ ZCL_ATTRID_BASIC_POWER_SOURCE,		ZCL_DATA_TYPE_ENUM8,	ACCESS_CONTROL_READ,	(u8*)&g_basicAttrs.powerSource},
	{ ZCL_ATTRID_BASIC_DEV_ENABLED,			ZCL_DATA_TYPE_BOOLEAN,	ACCESS_CONTROL_READ | ACCESS_CONTROL_WRITE,	(u8*)&g_basicAttrs.deviceEnable},
	{ ZCL_ATTRID_BASIC_SW_BUILD_ID,			ZCL_DATA_TYPE_CHAR_STR,	ACCESS_CONTROL_READ,	(u8*)g_basicAttrs.swBuildId},
	{ ZCL_ATTRID_GLOBAL_CLUSTER_REVISION,	ZCL_DATA_TYPE_UINT16,	ACCESS_CONTROL_READ,	(u8*)&zcl_attr_global_clusterRevision},
};

typedef struct{
	u8	batteryVoltage;			/* 0x0020, 100 mV units, 0xff unknown */
	u8	batteryPercentage;		/* 0x0021, 0.5 % units, 0xff unknown */
}zb_powerAttr_t;

static zb_powerAttr_t g_powerAttrs = {
	.batteryVoltage		= 0xff,
	.batteryPercentage	= 0xff,
};

static const zclAttrInfo_t powerCfg_attrTbl[] =
{
	{ ZCL_ATTRID_BATTERY_VOLTAGE,				ZCL_DATA_TYPE_UINT8,	ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE,	(u8*)&g_powerAttrs.batteryVoltage},
	{ ZCL_ATTRID_BATTERY_PERCENTAGE_REMAINING,	ZCL_DATA_TYPE_UINT8,	ACCESS_CONTROL_READ | ACCESS_CONTROL_REPORTABLE,	(u8*)&g_powerAttrs.batteryPercentage},
	{ ZCL_ATTRID_GLOBAL_CLUSTER_REVISION,		ZCL_DATA_TYPE_UINT16,	ACCESS_CONTROL_READ,	(u8*)&zcl_attr_global_clusterRevision},
};

static const zcl_specClusterInfo_t ep1_clusterList[] =
{
	{ZCL_CLUSTER_GEN_BASIC,		MANUFACTURER_CODE_NONE, sizeof(basic_attrTbl) / sizeof(zclAttrInfo_t),		basic_attrTbl,		zcl_basic_register,		NULL},
	{ZCL_CLUSTER_GEN_POWER_CFG,	MANUFACTURER_CODE_NONE, sizeof(powerCfg_attrTbl) / sizeof(zclAttrInfo_t),	powerCfg_attrTbl,	zcl_powerCfg_register,	NULL},
};

/**********************************************************************
 * Endpoints: ep 1 = button 1 + device clusters, ep 2..N = buttons 2..N
 */
static const u16 ep1_inClusters[] = {ZCL_CLUSTER_GEN_BASIC, ZCL_CLUSTER_GEN_POWER_CFG, ZCL_CLUSTER_GEN_ON_OFF};
static const u16 epN_inClusters[] = {ZCL_CLUSTER_GEN_ON_OFF};

#define ZB_DEVICE_ID_ONOFF_SWITCH	0x0000

#define EP_DESC(ep, in)	{HA_PROFILE_ID, ZB_DEVICE_ID_ONOFF_SWITCH, (ep), 1, 0, \
						 sizeof(in) / sizeof(u16), 0, (u16 *)(in), NULL}

static const af_simple_descriptor_t epDesc[ZB_EP_MAX] = {
	EP_DESC(1, ep1_inClusters),
	EP_DESC(2, epN_inClusters),
	EP_DESC(3, epN_inClusters),
	EP_DESC(4, epN_inClusters),
	EP_DESC(5, epN_inClusters),
	EP_DESC(6, epN_inClusters),
};

/**********************************************************************
 * State (all in retention RAM)
 */
static ev_timer_event_t *steerEvt;
static ev_timer_event_t *slowPollEvt;
static ev_timer_event_t *rejoinEvt;
static ev_timer_event_t *batteryEvt;
static ev_timer_event_t *resetEvt;

static u8 steerAttempts;
static u8 zbPairing;		/* steering in progress or retry pending: LED blinks */
static u8 pairArmed;		/* button 1 held 10 s, pairing starts on release */
static u32 ledSolidTick;	/* LED solid on (join success) since */
static u8 ledSolid;

static void zb_steer(void);

/**********************************************************************
 * Identity: model from the button count, manufacturer from the Tuya pid
 */
static void zb_setStr(u8 *dst, const char *a, const char *b)
{
	u8 n = 0;
	while(*a && n < ZCL_BASIC_MAX_LENGTH - 1){
		dst[1 + n++] = *a++;
	}
	while(b && *b && n < ZCL_BASIC_MAX_LENGTH - 1){
		dst[1 + n++] = *b++;
	}
	dst[0] = n;
}

static void zb_identityInit(void)
{
	char pid[TUYA_PID_LEN + 1];
	int ok = 1;

	flash_read_page(TUYA_PID_ADDR, TUYA_PID_LEN, (u8 *)pid);
	for(int i = 0; i < TUYA_PID_LEN; i++){
		char c = pid[i];
		if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))){
			ok = 0;
			break;
		}
	}
	pid[TUYA_PID_LEN] = 0;

	zb_setStr(g_basicAttrs.manuName, TUYA_MANU_PREFIX, ok ? pid : TUYA_PID_DEFAULT);
	zb_setStr(g_basicAttrs.modelId, (btn_count() == 6) ? "TS0046" : "TS0044", NULL);

	{
		char ver[] = "ss6400zb-v00.00";
		const char hex[] = "0123456789ABCDEF";
		ver[10] = hex[(APP_RELEASE >> 4) & 15];
		ver[11] = hex[APP_RELEASE & 15];
		ver[13] = hex[(APP_BUILD >> 4) & 15];
		ver[14] = hex[APP_BUILD & 15];
		zb_setStr(g_basicAttrs.swBuildId, ver, NULL);
	}

	log_printf("zb: %s / _TZ3000_%s", (btn_count() == 6) ? "TS0046" : "TS0044", ok ? pid : TUYA_PID_DEFAULT);
}

/**********************************************************************
 * NV: the stock Tuya firmware kept its own NV (same Telink format, other
 * module layout) at 0xD8000-0xEFFFF, overlapping our NV at 0xE0000. A
 * module header whose id doesn't match its slot is foreign: wipe our
 * whole NV area once, so the stack starts factory new.
 */
static void zb_nvSanitize(void)
{
	int foreign = 0;

	for(u8 id = 0; id < NV_MAX_MODULS && !foreign; id++){
		for(u8 sect = 0; sect < MODULE_SECTOR_NUM; sect++){
			nv_sect_info_t si;
			flash_read_page(MODULE_SECT_START(id, sect), sizeof(si), (u8 *)&si);
			if(si.usedFlag != NV_SECTOR_IDLE && si.idName != id){
				foreign = 1;
				break;
			}
		}
	}

	if(foreign){
		log_printf("zb: foreign NV found, erasing 0x%05X-0x%05X", NV_BASE_ADDRESS, CFG_NV_START_FOR_BLE - 1);
		for(u32 a = NV_BASE_ADDRESS; a < CFG_NV_START_FOR_BLE; a += FLASH_SECTOR_SIZE){
			flash_erase_sector(a);
		}
	}
}

/**********************************************************************
 * Battery: VDD through the PC5 trick
 */
static void zb_batteryMeasure(void)
{
	u16 mv;
	u8 pct;

	adc_channel_init(SHL_ADC_VBAT);		/* drives PC5 high */
	mv = get_adc_mv(0);
	gpio_set_output_en(GPIO_VBAT, 0);

	if(mv <= BATTERY_MV_MIN){
		pct = 0;
	}else if(mv >= BATTERY_MV_MAX){
		pct = 100;
	}else{
		pct = ((u32)(mv - BATTERY_MV_MIN) * 100) / (BATTERY_MV_MAX - BATTERY_MV_MIN);
	}

	g_powerAttrs.batteryVoltage = (mv + 50) / 100;
	g_powerAttrs.batteryPercentage = pct * 2;
	log_printf("battery %u mV, %u %%", mv, pct);
}

static void zb_coordDst(epInfo_t *dst, u8 ack)
{
	memset((u8 *)dst, 0, sizeof(epInfo_t));
	dst->dstAddrMode = APS_SHORT_DSTADDR_WITHEP;
	dst->dstAddr.shortAddr = 0x0000;
	dst->dstEp = 1;
	dst->profileId = HA_PROFILE_ID;
	if(ack){
		dst->txOptions |= APS_TX_OPT_ACK_TX;
	}
}

static void zb_batteryReport(void)
{
	epInfo_t dst;

	if(!zb_isDeviceJoinedNwk()){
		return;
	}
	zb_coordDst(&dst, 0);
	zcl_sendReportCmd(1, &dst, TRUE, ZCL_FRAME_SERVER_CLIENT_DIR, ZCL_CLUSTER_GEN_POWER_CFG,
					  ZCL_ATTRID_BATTERY_VOLTAGE, ZCL_DATA_TYPE_UINT8, &g_powerAttrs.batteryVoltage);
	zcl_sendReportCmd(1, &dst, TRUE, ZCL_FRAME_SERVER_CLIENT_DIR, ZCL_CLUSTER_GEN_POWER_CFG,
					  ZCL_ATTRID_BATTERY_PERCENTAGE_REMAINING, ZCL_DATA_TYPE_UINT8, &g_powerAttrs.batteryPercentage);
}

/* periodic: also guarantees there is always a timer pending, so the PM
 * code always picks deep sleep WITH retention (a timer-less deep sleep
 * would come back through a cold boot) */
static s32 zb_batteryTimerCb(void *arg)
{
	zb_batteryMeasure();
	zb_batteryReport();
	return 0;
}

/**********************************************************************
 * Poll rate
 */
static s32 zb_slowPollCb(void *arg)
{
	slowPollEvt = NULL;
	zb_setPollRate(ZB_POLL_SLOW_MS);
	return -1;
}

/* poll fast for a while: Z2M may be talking to us (interview, configure,
 * a read right after a press) */
static void zb_fastPoll(u32 ms)
{
	if(!zb_isDeviceJoinedNwk()){
		return;
	}
	zb_setPollRate(ZB_POLL_FAST_MS);
	if(slowPollEvt){
		TL_ZB_TIMER_CANCEL(&slowPollEvt);
	}
	slowPollEvt = TL_ZB_TIMER_SCHEDULE(zb_slowPollCb, NULL, ms);
}

/**********************************************************************
 * Buttons -> Tuya scene command
 */
static void zb_sendAction(int btn, u8 action)
{
	epInfo_t dst;
	u8 payload = action;

	if(!zb_isDeviceJoinedNwk()){
		log_printf("zb: not joined, button %u dropped", btn + 1);
		return;
	}

	zb_coordDst(&dst, 1);
	zcl_sendCmd(btn + 1, &dst, ZCL_CLUSTER_GEN_ON_OFF, ZCL_CMD_TUYA_ACTION, TRUE,
				ZCL_FRAME_CLIENT_SERVER_DIR, TRUE, MANUFACTURER_CODE_NONE, ZCL_SEQ_NUM, 1, &payload);

	/* the APS ack comes back through the parent: poll for it */
	zb_fastPoll(ZB_FAST_POLL_AFTER_KEY_MS);
}

/* leave (if joined) took too long or never confirmed: reset anyway */
static s32 zb_resetCb(void *arg)
{
	nv_resetToFactoryNew();
	SYSTEM_RESET();
	return -1;
}

static void zb_startPairing(void)
{
	if(zb_isDeviceFactoryNew()){
		log_printf("zb: pairing (factory new)");
		steerAttempts = 0;
		zb_steer();
		return;
	}

	/* joined: leave, back to factory new, reboot; the fresh boot finds
	 * a factory-new device and starts steering */
	log_printf("zb: leave + factory reset, then pairing");
	zbPairing = 1;
	zb_factoryReset();
	if(!resetEvt){
		resetEvt = TL_ZB_TIMER_SCHEDULE(zb_resetCb, NULL, 3000);
	}
}

void zb_buttonEvent(int btn, u8 ev)
{
	switch(ev){
	case BTN_EV_SHORT:
		zb_sendAction(btn, TUYA_ACTION_SINGLE);
		break;
	case BTN_EV_LONG:
		zb_sendAction(btn, TUYA_ACTION_HOLD);
		break;
	case BTN_EV_VLONG:
		if(btn == 0){
			pairArmed = 1;		/* LED blinks fast: release to start */
		}
		break;
	case BTN_EV_VLONG_UP:
		if(btn == 0 && pairArmed){
			pairArmed = 0;
			zb_startPairing();
		}
		break;
	default:
		break;
	}
}

/**********************************************************************
 * Commissioning
 */
static s32 zb_steerCb(void *arg)
{
	steerEvt = NULL;
	zb_steer();
	return -1;
}

static void zb_steer(void)
{
	zbPairing = 1;
	steerAttempts++;
	log_printf("zb: network steering, attempt %u/%u", steerAttempts, ZB_STEER_ATTEMPTS);
	bdb_networkSteerStart();
}

static void zb_steerLater(u32 ms)
{
	if(steerEvt){
		TL_ZB_TIMER_CANCEL(&steerEvt);
	}
	steerEvt = TL_ZB_TIMER_SCHEDULE(zb_steerCb, NULL, ms + (zb_random() & 0x1ff));
}

static s32 zb_rejoinCb(void *arg)
{
	if(zb_isDeviceFactoryNew()){
		rejoinEvt = NULL;
		return -1;
	}
	zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
	return 0;
}

static void zb_bdbInitCb(u8 status, u8 joinedNetwork)
{
	log_printf("zb: bdb init status %u joined %u", status, joinedNetwork);

	if(status == BDB_INIT_STATUS_SUCCESS){
		if(joinedNetwork){
			zb_setPollRate(ZB_POLL_SLOW_MS);
		}else{
			/* factory new at power-up: try to join right away */
			steerAttempts = 0;
			zbPairing = 1;
			zb_steerLater(500);
		}
	}else if(joinedNetwork){
		zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
	}
}

static void zb_bdbCommissioningCb(u8 status, void *arg)
{
	log_printf("zb: commissioning status %u", status);

	switch(status){
	case BDB_COMMISSION_STA_SUCCESS:
		zbPairing = 0;
		steerAttempts = 0;
		if(steerEvt){
			TL_ZB_TIMER_CANCEL(&steerEvt);
		}
		if(rejoinEvt){
			TL_ZB_TIMER_CANCEL(&rejoinEvt);
		}
		/* 1 s solid LED: joined */
		ledSolid = 1;
		ledSolidTick = clock_time();
		gpio_write(GPIO_LED, LED_ON);

		zb_fastPoll(ZB_FAST_POLL_AFTER_JOIN_MS);
		zb_batteryReport();
		break;

	case BDB_COMMISSION_STA_IN_PROGRESS:
		break;

	case BDB_COMMISSION_STA_NO_NETWORK:
	case BDB_COMMISSION_STA_TCLK_EX_FAILURE:
	case BDB_COMMISSION_STA_TARGET_FAILURE:
	case BDB_COMMISSION_STA_FORMATION_FAILURE:
		if(zb_isDeviceJoinedNwk()){
			break;
		}
		if(steerAttempts < ZB_STEER_ATTEMPTS){
			zb_steerLater(2000);
		}else{
			log_printf("zb: no network found, giving up (hold button 1 for 10 s to retry)");
			zbPairing = 0;
		}
		break;

	case BDB_COMMISSION_STA_NO_SCAN_RESPONSE:
	case BDB_COMMISSION_STA_PARENT_LOST:
		zb_rejoinReqWithBackOff(zb_apsChannelMaskGet(), g_bdbAttrs.scanDuration);
		break;

	case BDB_COMMISSION_STA_REJOIN_FAILURE:
		if(!zb_isDeviceFactoryNew() && !rejoinEvt){
			rejoinEvt = TL_ZB_TIMER_SCHEDULE(zb_rejoinCb, NULL, ZB_REJOIN_BACKOFF_MS);
		}
		break;

	default:
		break;
	}
}

static void zb_bdbIdentifyCb(u8 endpoint, u16 srcAddr, u16 identifyTime)
{
}

static bdb_appCb_t zb_bdbCb = {
	zb_bdbInitCb,
	zb_bdbCommissioningCb,
	zb_bdbIdentifyCb,
	NULL
};

static void zb_leaveCnfHandler(nlme_leave_cnf_t *pLeaveCnf)
{
	log_printf("zb: leave cnf %u", pLeaveCnf->status);
	zb_resetCb(NULL);
}

static void zb_leaveIndHandler(nlme_leave_ind_t *pLeaveInd)
{
}

static const zdo_appIndCb_t zb_zdoCb = {
	bdb_zdoStartDevCnf,		/* start device cnf */
	NULL,					/* reset cnf */
	NULL,					/* device announce ind */
	zb_leaveIndHandler,		/* leave ind */
	zb_leaveCnfHandler,		/* leave cnf */
	NULL,					/* nwk update ind */
	NULL,					/* permit join ind */
	NULL,					/* nlme sync cnf */
	NULL,					/* tc join ind */
	NULL,					/* tc frame counter near limit */
};

static bdb_commissionSetting_t zb_commissionSetting = {
	.linkKey.tcLinkKey.keyType = SS_GLOBAL_LINK_KEY,
	.linkKey.tcLinkKey.key = (u8 *)tcLinkKeyCentralDefault,

	.linkKey.distributeLinkKey.keyType = MASTER_KEY,
	.linkKey.distributeLinkKey.key = (u8 *)linkKeyDistributedMaster,

	.linkKey.touchLinkKey.keyType = MASTER_KEY,
	.linkKey.touchLinkKey.key = (u8 *)touchLinkKeyMaster,

	.touchlinkEnable = 0,
	.touchlinkChannel = DEFAULT_CHANNEL,
	.touchlinkLqiThreshold = 0xA0,
};

static struct{
	u8 keyType;
	u8 key[16];
}zb_tcLinkKey;

static void zb_zclHook(zclIncoming_t *pInHdlrMsg)
{
}

static void zb_stackInit(u8 isRetention)
{
	if(isRetention){
		/* re-config phy when system recovers from deep sleep with retention */
		mac_phyReconfig();
		return;
	}

	zb_init();
	zb_zdoCbRegister((zdo_appIndCb_t *)&zb_zdoCb);

	af_powerDescPowerModeUpdate(POWER_MODE_RECEIVER_COMES_WHEN_STIMULATED);
	zcl_init(zb_zclHook);		/* also randomizes the ZCL sequence number */

	for(int i = 0; i < btn_count() && i < ZB_EP_MAX; i++){
		af_endpointRegister(i + 1, (af_simple_descriptor_t *)&epDesc[i], zcl_rx_handler, NULL);
	}

	zcl_reportingTabInit();
	zcl_register(1, sizeof(ep1_clusterList) / sizeof(ep1_clusterList[0]), (zcl_specClusterInfo_t *)ep1_clusterList);

	if(bdb_preInstallCodeLoad(&zb_tcLinkKey.keyType, zb_tcLinkKey.key) == RET_OK){
		zb_commissionSetting.linkKey.tcLinkKey.keyType = zb_tcLinkKey.keyType;
		zb_commissionSetting.linkKey.tcLinkKey.key = zb_tcLinkKey.key;
	}

	u8 repower = drv_pm_deepSleep_flag_get() ? 0 : 1;
	bdb_init((af_simple_descriptor_t *)&epDesc[0], &zb_commissionSetting, &zb_bdbCb, repower);
}

/**********************************************************************
 * LED, power, main loop
 */
static void zb_ledTask(void)
{
	if(ledSolid){
		if(clock_time_exceed(ledSolidTick, 1000 * 1000)){
			ledSolid = 0;
			gpio_write(GPIO_LED, LED_OFF);
		}
		return;
	}

	if(pairArmed || zbPairing){
		/* armed (waiting for the release): 10 Hz; steering: 2 Hz */
		u32 halfMs = pairArmed ? 50 : 250;
		u32 phase = (clock_time() / (halfMs * 1000 * sysTimerPerUs)) & 1;
		gpio_write(GPIO_LED, phase ? LED_ON : LED_OFF);
		return;
	}

	/* event blip from buttons.c */
	if(led_on && clock_time_exceed(led_tick, LED_ON_MS * 1000)){
		led_on = 0;
		gpio_write(GPIO_LED, LED_OFF);
	}
}

static void zb_pmTask(void)
{
#if PM_ENABLE
	/* awake while a button is held (holds are short, and the release must
	 * be seen) and while the LED shows something: GPIO outputs don't
	 * survive deep sleep */
	if(btn_anyDown() || led_on || ledSolid || pairArmed || zbPairing){
		return;
	}
	if(!bdb_isIdle() || tl_stackBusy() || !zb_isTaskDone()){
		return;
	}

	gpio_write(GPIO_LED, LED_OFF);
	btn_armWakeAll();
	drv_pm_lowPowerEnter();
#endif
}

static void zb_task(void)
{
	ev_main();
	tl_zbTaskProcedure();
}

/* Zigbee-only interrupt handler (irq.c dispatches here when not in
 * service mode). No BLE: the system timer IRQ (enabled by ZB_TIMER_INIT
 * but used by nothing in this build) is just acknowledged. */
_attribute_ram_code_ void zb_irqHandler(void)
{
	u32 src = reg_irq_src;
	u16 src_rf = rf_irq_src_get();

	if(src & FLD_IRQ_SYSTEM_TIMER){
		reg_irq_src = FLD_IRQ_SYSTEM_TIMER;
	}

	if(src_rf & FLD_RF_IRQ_TX){
		rf_irq_clr_src(FLD_RF_IRQ_TX);
		rf_tx_irq_handler();
	}
	if(src_rf & FLD_RF_IRQ_RX){
		rf_irq_clr_src(FLD_RF_IRQ_RX);
		rf_rx_irq_handler();
	}

	if(src & FLD_IRQ_TMR0_EN){
		reg_irq_src = FLD_IRQ_TMR0_EN;
		reg_tmr_sta = FLD_TMR_STA_TMR0;
		drv_timer_irq0_handler();
	}
	if(src & FLD_IRQ_TMR1_EN){
		reg_irq_src = FLD_IRQ_TMR1_EN;
		reg_tmr_sta = FLD_TMR_STA_TMR1;
		drv_timer_irq1_handler();
	}
}

void zb_main(u8 isRetention)
{
	CURRENT_SLOT_SET(DUALMODE_SLOT_ZIGBEE);

	if(!isRetention){
		zb_nvSanitize();
		zb_identityInit();
		zb_batteryMeasure();
	}

	os_init(isRetention);
	zb_stackInit(isRetention);

	if(!isRetention){
		batteryEvt = TL_ZB_TIMER_SCHEDULE(zb_batteryTimerCb, NULL, BATTERY_REPORT_PERIOD_S * 1000);
	}

	drv_enable_irq();

	while(1){
		zb_task();
		log_tick();
		btn_task();
		zb_ledTask();
		zb_pmTask();
	}
}
