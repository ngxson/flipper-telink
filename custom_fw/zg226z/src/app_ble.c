/**********************************************************************
 * BLE side of the ZG-228Z.
 *
 * The TX pad (PB1) bridged to the RX pad (PB7) selects BLE mode: the LED
 * blinks fast, advertising is on, and a GATT service lets you
 *  - write an RTTTL string to the "play" characteristic to hear it now
 *  - write an RTTTL string to a "slot1..3" characteristic to store it
 *    (and read it back)
 * Removing the bridge reboots the device back into Zigbee-only mode.
 *
 * Writes are chunked: append bytes until a 0x00 terminator arrives, then
 * the buffered string is validated and committed. This also works with
 * the default 23-byte MTU.
 **********************************************************************/
#include "tl_common.h"
#include "device.h"
#include "app.h"
#include "app_cfg.h"
#include "board.h"
#include "buzzer.h"
#include "app_ui.h"
#include "ble_cfg.h"
#include "zigbee_ble_switch.h"

/* simple busy-wait (device.h of BZdevice style) */
#define pm_wait_ms(t)	sleep_us((t)*1000)

#include "stack/ble/ble.h"
#include "stack/ble/ble_config.h"
#include "stack/ble/ble_common.h"
#include "flash.h"

#define RX_FIFO_SIZE	64
#define RX_FIFO_NUM		8
#define TX_FIFO_SIZE	40
#define TX_FIFO_NUM		8

u8 mac_public[6];

/* staging for chunked writes */
static struct{
	u8 buf[RTTTL_MAX_LEN];
	u16 len;
} wrStage;

/* the attribute table is non-const: slot values point into RAM buffers */
static const u16 my_primaryServiceUUID = GATT_UUID_PRIMARY_SERVICE;
static const u16 my_characterUUID = GATT_UUID_CHARACTER;
static const u16 clientCharacterCfgUUID = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 my_gapServiceUUID = SERVICE_UUID_GENERIC_ACCESS;
static const u16 my_gattServiceUUID = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const u16 my_devNameUUID = GATT_UUID_DEVICE_NAME;
static const u16 my_serviceChangeUIID = GATT_UUID_SERVICE_CHANGE;
static const u16 my_appearanceUIID = 0x2a01;

static const u8 PROP_READ = CHAR_PROP_READ;
static const u8 PROP_INDICATE = CHAR_PROP_INDICATE;
static const u8 PROP_READ_WRITE_NORSP = CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP;
static const u8 PROP_WRITE_NORSP = CHAR_PROP_WRITE_WITHOUT_RSP;

/* GATT write callbacks (used in the attribute table below) */
int app_blePlayWrite(void *p);
int app_bleSlot1Write(void *p);
int app_bleSlot2Write(void *p);
int app_bleSlot3Write(void *p);

static u16 serviceChangeVal[2] = {0, 0};
static u8 serviceChangeCCC[2] = {0, 0};
static u16 my_appearance = GAP_APPEARE_UNKNOWN;

/* 128-bit UUIDs, byte order as used in the attribute table */
/* 0000XXXX-0000-1000-8000-00805f9b34fb, little endian as BLE sends it */
#define BT_BASE_UUID16(lo, hi)	{0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, (lo), (hi), 0x00, 0x00}

static const u8 UUID_SVC[16]   = BT_BASE_UUID16(ZG228Z_SVC_UUID16 & 0xff, (ZG228Z_SVC_UUID16 >> 8) & 0xff);
static const u8 UUID_PLAY[16]  = BT_BASE_UUID16(ZG228Z_CHAR_PLAY_UUID16 & 0xff, (ZG228Z_CHAR_PLAY_UUID16 >> 8) & 0xff);
static const u8 UUID_SLOT1[16] = BT_BASE_UUID16(ZG228Z_CHAR_SLOT1_UUID16 & 0xff, (ZG228Z_CHAR_SLOT1_UUID16 >> 8) & 0xff);
static const u8 UUID_SLOT2[16] = BT_BASE_UUID16(ZG228Z_CHAR_SLOT2_UUID16 & 0xff, (ZG228Z_CHAR_SLOT2_UUID16 >> 8) & 0xff);
static const u8 UUID_SLOT3[16] = BT_BASE_UUID16(ZG228Z_CHAR_SLOT3_UUID16 & 0xff, (ZG228Z_CHAR_SLOT3_UUID16 >> 8) & 0xff);

static u8 devName[12];			/* "ZG228z" + 4 hex digits of the MAC */

static u8 playDummy;			/* attribute values for write-only chars */

static const attribute_t my_Attributes[] = {
	/* ATT_END_H - 1 total attributes */
	{ATT_END_H - 1, 0, 0, 0, 0, 0, NULL, NULL},

	/* Generic Access */
	{5, ATT_PERMISSIONS_READ, 2, sizeof(my_gapServiceUUID), (u8*)(&my_primaryServiceUUID), (u8*)(&my_gapServiceUUID), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ), (u8*)(&my_characterUUID), (u8*)(&PROP_READ), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(devName), (u8*)(&my_devNameUUID), (u8*)(devName), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, 1, (u8*)(&my_characterUUID), (u8*)(&PROP_READ), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearance), (u8*)(&my_appearanceUIID), (u8*)(&my_appearance), NULL, NULL},

	/* GATT */
	{4, ATT_PERMISSIONS_READ, 2, sizeof(my_gattServiceUUID), (u8*)(&my_primaryServiceUUID), (u8*)(&my_gattServiceUUID), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_INDICATE), (u8*)(&my_characterUUID), (u8*)(&PROP_INDICATE), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(serviceChangeVal), (u8*)(&my_serviceChangeUIID), (u8*)(&serviceChangeVal), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 2, sizeof(serviceChangeCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(serviceChangeCCC), NULL, NULL},

	/* ZG-228Z melody service */
	{9, ATT_PERMISSIONS_READ, 2, 16, (u8*)(&my_primaryServiceUUID), (u8*)(UUID_SVC), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_WRITE_NORSP), (u8*)(&my_characterUUID), (u8*)(&PROP_WRITE_NORSP), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 16, sizeof(playDummy), (u8*)(UUID_PLAY), (u8*)(&playDummy), &app_blePlayWrite, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ_WRITE_NORSP), (u8*)(&my_characterUUID), (u8*)(&PROP_READ_WRITE_NORSP), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 16, RTTTL_MAX_LEN, (u8*)UUID_SLOT1, (u8*)(buzzer_melodyRam[0]), &app_bleSlot1Write, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ_WRITE_NORSP), (u8*)(&my_characterUUID), (u8*)(&PROP_READ_WRITE_NORSP), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 16, RTTTL_MAX_LEN, (u8*)UUID_SLOT2, (u8*)(buzzer_melodyRam[1]), &app_bleSlot2Write, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ_WRITE_NORSP), (u8*)(&my_characterUUID), (u8*)(&PROP_READ_WRITE_NORSP), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 16, RTTTL_MAX_LEN, (u8*)UUID_SLOT3, (u8*)(buzzer_melodyRam[2]), &app_bleSlot3Write, NULL},
};

_attribute_data_retention_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_rxfifo = {RX_FIFO_SIZE, RX_FIFO_NUM, 0, 0, blt_rxfifo_b};
_attribute_data_retention_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM] = {0};
_attribute_data_retention_ my_fifo_t blt_txfifo = {TX_FIFO_SIZE, TX_FIFO_NUM, 0, 0, blt_txfifo_b};

_attribute_data_retention_ own_addr_type_t app_own_address_type = OWN_ADDRESS_PUBLIC;

u8 g_ble_txPowerSet = BLE_DEFAULT_TX_POWER_IDX;
_attribute_data_retention_ int device_in_connection_state = 0;
_attribute_data_retention_ u8 sendTerminate_before_enterDeep = 0;
#if (MTU_SIZE_SETTING)
_attribute_data_retention_ int mtuExchange_started_flg = 0;
#endif
volatile bool g_bleConnDoing = 0;

/**********************************************************************
 * Chunked writes: append until a 0x00 byte arrives
 */

static int ble_rxAppend(rf_packet_att_data_t *req, u8 len)
{
	u8 *dat = req->dat;
	u16 i;

	for(i = 0; i < len; i++){
		if(dat[i] == 0x00){
			/* terminator: commit */
			return 1;
		}
		if(wrStage.len < RTTTL_MAX_LEN - 1){
			wrStage.buf[wrStage.len++] = dat[i];
		}else{
			/* too long: drop the whole staged string */
			wrStage.len = 0;
		}
	}
	return 0;
}

int app_blePlayWrite(void *p)
{
	rf_packet_att_data_t *req = (rf_packet_att_data_t *)p;
	u8 len = req->rf_len - 7;	/* l2cap hdr 4 + ATT opcode 1 + handle 2 */

	if(ble_rxAppend(req, len)){
		if(wrStage.len){
			wrStage.buf[wrStage.len] = 0;
			buzzer_play((const char *)wrStage.buf, FALSE, 0);	/* explicit play request; invalid RTTTL stays silent */
		}
		wrStage.len = 0;
	}
	return 0;
}

static int ble_slotWrite(void *p, u8 slot)
{
	rf_packet_att_data_t *req = (rf_packet_att_data_t *)p;
	u8 len = req->rf_len - 7;	/* l2cap hdr 4 + ATT opcode 1 + handle 2 */
	u8 ok;

	if(ble_rxAppend(req, len)){
		if(wrStage.len){
			wrStage.buf[wrStage.len] = 0;
			ok = buzzer_setMelody(slot, (const char *)wrStage.buf, (u8)wrStage.len);
		}else{
			ok = TRUE;
		}
		(void)ok;	/* no confirmation sound: read the slot back to verify */
		wrStage.len = 0;
	}
	return 0;
}

int app_bleSlot1Write(void *p) { return ble_slotWrite(p, 0); }
int app_bleSlot2Write(void *p) { return ble_slotWrite(p, 1); }
int app_bleSlot3Write(void *p) { return ble_slotWrite(p, 2); }

/**********************************************************************
 * GAP events / connection callbacks
 */
static void app_switch_to_indirect_adv(u8 e, u8 *p, int n)
{
	bls_ll_setAdvParam(DEF_ADV_INTERVAL_MIN, DEF_ADV_INTERVAL_MAX,
						ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC,
						0, NULL,
						DEF_APP_ADV_CHANNEL,
						ADV_FP_NONE);

	bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}

static void ble_remote_terminate(u8 e, u8 *p, int n)
{
	device_in_connection_state = 0;
#if (MTU_SIZE_SETTING)
	mtuExchange_started_flg = 0;
#endif

	bls_ll_setAdvEnable(BLC_ADV_DISABLE);
	if(*p != HCI_ERR_OP_CANCELLED_BY_HOST){
		app_switch_to_indirect_adv(0, 0, 0);
	}
}

static void user_set_rf_power(u8 e, u8 *p, int n)
{
	rf_set_power_level_index(g_ble_txPowerSet);
}

static void task_connect(u8 e, u8 *p, int n)
{
	bls_l2cap_requestConnParamUpdate(DEF_CON_PAR_UPDATE);
	device_in_connection_state = 1;
}

static void blc_initMacAddress(int flash_addr, u8 *mac_public, u8 *mac_random_static)
{
	u8 mac_read[8];
	u8 value_rand[5];
	u8 ff_six_byte[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	flash_read_page(flash_addr, 8, mac_read);
	if(memcmp(mac_read, ff_six_byte, sizeof(mac_read))){
		memcpy(mac_public, mac_read, 6);
	}else{
		/* no public address in flash: keep the Zigbee MAC area untouched
		 * (never erase 0x76000) - use a random static-style BLE address */
		generateRandomNum(sizeof(value_rand), value_rand);
		mac_public[0] = value_rand[0];
		mac_public[1] = value_rand[1];
		mac_public[2] = value_rand[2];
		mac_public[3] = 0x38;
		mac_public[4] = 0xC1;
		mac_public[5] = 0xA4;
	}

	mac_random_static[0] = mac_public[0];
	mac_random_static[1] = mac_public[1];
	mac_random_static[2] = mac_public[2];
	mac_random_static[3] = value_rand[3];
	mac_random_static[4] = value_rand[4];
	mac_random_static[5] = 0xC0;
}

int app_host_event_callback(u32 h, u8 *para, int n)
{
	u8 event = h & 0xFF;

	switch(event){
		case GAP_EVT_SMP_PARING_BEAGIN:
			g_bleConnDoing = 1;
			break;
		case GAP_EVT_SMP_PARING_SUCCESS:
		case GAP_EVT_SMP_PARING_FAIL:
			g_bleConnDoing = 0;
			break;
		case GAP_EVT_SMP_CONN_ENCRYPTION_DONE:
#if (MTU_SIZE_SETTING)
			if(!mtuExchange_started_flg){
				blc_att_requestMtuSizeExchange(BLS_CONN_HANDLE, MTU_SIZE_SETTING);
			}
#endif
			break;
		case GAP_EVT_ATT_EXCHANGE_MTU:
#if (MTU_SIZE_SETTING)
			mtuExchange_started_flg = 1;
#endif
			break;
		default:
			break;
	}

	return 0;
}

/**********************************************************************
 * Advertising data
 */
static void app_setAdvData(void)
{
	/* flags + 128-bit service UUID */
	static const u8 tbl_advData[] = {
		0x02, 0x01, 0x06,					/* flags: LE general discoverable, BR/EDR not supported */
		0x11, 0x07,							/* complete list of 128-bit service class UUIDs */
		0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xE0, 0xFF, 0x00, 0x00,
	};

	bls_ll_setAdvData((u8 *)tbl_advData, sizeof(tbl_advData));
}

static char hexDigit(u8 v)
{
	return (v < 10) ? ('0' + v) : ('A' + v - 10);
}

/**********************************************************************
 * Init
 */
static void my_att_init(void)
{
	/* device name: "ZG228z" + 4 MAC hex digits */
	devName[0] = 'Z';
	devName[1] = 'G';
	devName[2] = '2';
	devName[3] = '2';
	devName[4] = '8';
	devName[5] = 'z';
	devName[6] = hexDigit(mac_public[2] >> 4);
	devName[7] = hexDigit(mac_public[2] & 0xf);
	devName[8] = hexDigit(mac_public[1] >> 4);
	devName[9] = hexDigit(mac_public[1] & 0xf);
	devName[10] = 0;

	/* slot attributes read the melody RAM buffers directly
	 * (attrLen was set to RTTTL_MAX_LEN in the table) */

	bls_att_setAttributeTable((u8 *)my_Attributes);
}

static void user_ble_normal_init(void)
{
	u8 mac_random_static[6];

	bls_smp_configParingSecurityInfoStorageAddr(CFG_NV_START_FOR_BLE);

	blc_initMacAddress(CFG_MAC_ADDRESS, mac_public, mac_random_static);

#if (BLE_DEVICE_ADDRESS_TYPE == BLE_DEVICE_ADDRESS_RANDOM_STATIC)
	blc_ll_setRandomAddr(mac_random_static);
#endif

	/* Controller */
	blc_ll_initBasicMCU();
	blc_ll_initStandby_module(mac_public);
	blc_ll_initAdvertising_module(mac_public);
	blc_ll_initSlaveRole_module();
	blc_ll_initPowerManagement_module();

	/* Host */
	blc_gap_peripheral_init();
	my_att_init();
	blc_l2cap_register_handler(blc_l2cap_packet_receive);
	blc_att_setRxMtuSize(MTU_SIZE_SETTING);

#if (APP_SECURITY_ENABLE)
	blc_smp_peripheral_init();
#else
	blc_smp_setSecurityLevel(No_Security);
#endif

	blc_gap_registerHostEventHandler(app_host_event_callback);
	blc_gap_setEventMask(GAP_EVT_MASK_SMP_PARING_BEAGIN |
						 GAP_EVT_MASK_SMP_PARING_SUCCESS |
						 GAP_EVT_MASK_SMP_PARING_FAIL |
						 GAP_EVT_MASK_SMP_CONN_ENCRYPTION_DONE |
						 GAP_EVT_MASK_ATT_EXCHANGE_MTU);

	/* advertising setup (enabled later, only in BLE mode) */
	app_setAdvData();
	{
		/* scan response: complete local name ("ZG228zXXXX") */
		static u8 scanRsp[2 + 10];
		scanRsp[0] = 1 + 10;
		scanRsp[1] = 0x09;	/* GAP_ADTYPE_LOCAL_NAME_COMPLETE */
		memcpy(&scanRsp[2], devName, 10);
		bls_ll_setScanRspData(scanRsp, sizeof(scanRsp));
	}
	app_switch_to_indirect_adv(0, 0, 0);
	bls_ll_setAdvEnable(BLC_ADV_DISABLE);

	user_set_rf_power(0, 0, 0);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);

	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &task_connect);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &ble_remote_terminate);

	/* power management */
#if (BLE_APP_PM_ENABLE)
	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#else
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif
}

void user_ble_init(bool isRetention)
{
	sendTerminate_before_enterDeep = 0;
	if(isRetention){
		blc_ll_initBasicMCU();
		rf_set_power_level_index(g_ble_txPowerSet);
		blc_ll_recoverDeepRetention();
	}else{
		user_ble_normal_init();
	}
}

/* Enter BLE-only mode (TX pad bridged to GND at boot) */
void app_ble_enterBleOnly(void)
{
	g_appCtx.bleOnly = 1;
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);
	led_blink_start(0, 100, 100);	/* fast LED blink while bridged */
}

/* Called from the main loop in BLE-only mode */
void app_ble_task(void)
{
	/* bridge removed -> back to normal Zigbee operation (reboot) */
	static u32 lastCheck;
	static u8 misses;

	if(!clock_time_exceed(lastCheck, 1000 * 1000)){
		return;
	}
	lastCheck = clock_time();
	if(ble_bridge_detect()){
		misses = 0;
	}else if(++misses >= 2){
		SYSTEM_RESET();
	}
}
