/**********************************************************************
 * BLE peripheral: always advertising, no security.
 *
 * Services: GAP, GATT, Device Information (firmware revision string,
 * shows version and running slot) and the Telink OTA service
 * (00010203-0405-0607-0809-0a0b0c0d1912, data char ...2b12), whose writes
 * go straight to the library's otaWrite.
 **********************************************************************/
#include "tl_common.h"
#include "stack/ble/ble.h"
#include "stack/ble/ble_config.h"
#include "stack/ble/ble_common.h"
#include "ble_cfg.h"
#include "ble_ota.h"
#include "log.h"
#include "fp.h"

#define RX_FIFO_SIZE	64
#define RX_FIFO_NUM		8
#define TX_FIFO_SIZE	40
#define TX_FIFO_NUM		8

u8 mac_public[6];

static const u16 my_primaryServiceUUID = GATT_UUID_PRIMARY_SERVICE;
static const u16 my_characterUUID = GATT_UUID_CHARACTER;
static const u16 clientCharacterCfgUUID = GATT_UUID_CLIENT_CHAR_CFG;
static const u16 userdesc_UUID = GATT_UUID_CHAR_USER_DESC;
static const u16 my_gapServiceUUID = SERVICE_UUID_GENERIC_ACCESS;
static const u16 my_gattServiceUUID = SERVICE_UUID_GENERIC_ATTRIBUTE;
static const u16 my_devInfoServiceUUID = SERVICE_UUID_DEVICE_INFORMATION;
static const u16 my_devNameUUID = GATT_UUID_DEVICE_NAME;
static const u16 my_serviceChangeUUID = GATT_UUID_SERVICE_CHANGE;
static const u16 my_appearanceUUID = GATT_UUID_APPEARANCE;
static const u16 my_fwRevUUID = CHARACTERISTIC_UUID_FW_REVISION_STRING;

static const u8 PROP_READ = CHAR_PROP_READ;
static const u8 PROP_INDICATE = CHAR_PROP_INDICATE;
static const u8 PROP_READ_WRITE_NORSP = CHAR_PROP_READ | CHAR_PROP_WRITE_WITHOUT_RSP;
static const u8 PROP_NOTIFY = CHAR_PROP_NOTIFY;
static const u8 PROP_WRITE = CHAR_PROP_WRITE | CHAR_PROP_WRITE_WITHOUT_RSP;

static u16 serviceChangeVal[2] = {0, 0};
static u8 serviceChangeCCC[2] = {0, 0};
static const u16 my_appearance = GAP_APPEARE_UNKNOWN;

/* Telink OTA UUIDs, little endian */
static const u8 my_OtaServiceUUID[16] = {0x12, 0x19, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};
static const u8 my_OtaUUID[16]        = {0x12, 0x2b, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00};
static const u8 my_OtaName[] = {'O', 'T', 'A'};
static u8 my_OtaData;

/* 0000XXXX-0000-1000-8000-00805f9b34fb, little endian */
#define BT_BASE_UUID16(u)	{0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, (u) & 0xff, (u) >> 8, 0x00, 0x00}
static const u8 my_dbgServiceUUID[16] = BT_BASE_UUID16(DBG_SVC_UUID16);
static const u8 my_dbgLogUUID[16] = BT_BASE_UUID16(DBG_LOG_UUID16);
static const u8 my_dbgCmdUUID[16] = BT_BASE_UUID16(DBG_CMD_UUID16);
static u8 dbgLogVal;
static u8 dbgLogCCC[2];
static u8 dbgCmdVal;

static u8 devName[DEV_NAME_LEN];	/* "SS6400_" + 4 MAC hex digits */
static u8 fwRev[16];				/* "vRR.BB @0xXXXXX" */

/* debug service state */
static char cmdBuf[24];
static volatile u8 cmdPending;
static u32 logCursor;

/* CCC write: (re)subscribing streams the whole buffered log */
static int app_dbgCccWrite(void *p)
{
	rf_packet_att_data_t *req = (rf_packet_att_data_t *)p;
	dbgLogCCC[0] = req->dat[0];
	dbgLogCCC[1] = req->dat[1];
	logCursor = log_oldest();
	return 0;
}

static int app_dbgCmdWrite(void *p)
{
	rf_packet_att_data_t *req = (rf_packet_att_data_t *)p;
	int len = req->rf_len - 7;	/* l2cap hdr 4 + ATT opcode 1 + handle 2 */

	if(cmdPending || len <= 0){
		return 0;
	}
	if(len > (int)sizeof(cmdBuf) - 1){
		len = sizeof(cmdBuf) - 1;
	}
	memcpy(cmdBuf, req->dat, len);
	while(len && (cmdBuf[len - 1] == '\n' || cmdBuf[len - 1] == '\r' || cmdBuf[len - 1] == 0)){
		len--;
	}
	cmdBuf[len] = 0;
	cmdPending = 1;	/* run from the main loop, not from the ATT callback */
	return 0;
}

static const attribute_t my_Attributes[] = {
	{ATT_END_H - 1, 0, 0, 0, 0, 0, NULL, NULL},

	/* Generic Access */
	{5, ATT_PERMISSIONS_READ, 2, sizeof(my_gapServiceUUID), (u8*)(&my_primaryServiceUUID), (u8*)(&my_gapServiceUUID), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ), (u8*)(&my_characterUUID), (u8*)(&PROP_READ), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(devName), (u8*)(&my_devNameUUID), (u8*)(devName), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ), (u8*)(&my_characterUUID), (u8*)(&PROP_READ), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(my_appearance), (u8*)(&my_appearanceUUID), (u8*)(&my_appearance), NULL, NULL},

	/* GATT */
	{4, ATT_PERMISSIONS_READ, 2, sizeof(my_gattServiceUUID), (u8*)(&my_primaryServiceUUID), (u8*)(&my_gattServiceUUID), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_INDICATE), (u8*)(&my_characterUUID), (u8*)(&PROP_INDICATE), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(serviceChangeVal), (u8*)(&my_serviceChangeUUID), (u8*)(&serviceChangeVal), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 2, sizeof(serviceChangeCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(serviceChangeCCC), NULL, NULL},

	/* Device Information */
	{3, ATT_PERMISSIONS_READ, 2, sizeof(my_devInfoServiceUUID), (u8*)(&my_primaryServiceUUID), (u8*)(&my_devInfoServiceUUID), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ), (u8*)(&my_characterUUID), (u8*)(&PROP_READ), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(fwRev) - 1, (u8*)(&my_fwRevUUID), (u8*)(fwRev), NULL, NULL},

	/* Telink OTA */
	{4, ATT_PERMISSIONS_READ, 2, 16, (u8*)(&my_primaryServiceUUID), (u8*)(my_OtaServiceUUID), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_READ_WRITE_NORSP), (u8*)(&my_characterUUID), (u8*)(&PROP_READ_WRITE_NORSP), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 16, sizeof(my_OtaData), (u8*)(my_OtaUUID), (&my_OtaData), &otaWrite, &otaRead},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(my_OtaName), (u8*)(&userdesc_UUID), (u8*)(my_OtaName), NULL, NULL},

	/* debug: log + commands */
	{6, ATT_PERMISSIONS_READ, 2, 16, (u8*)(&my_primaryServiceUUID), (u8*)(my_dbgServiceUUID), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_NOTIFY), (u8*)(&my_characterUUID), (u8*)(&PROP_NOTIFY), NULL, NULL},
	{0, ATT_PERMISSIONS_READ, 16, sizeof(dbgLogVal), (u8*)(my_dbgLogUUID), (&dbgLogVal), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 2, sizeof(dbgLogCCC), (u8*)(&clientCharacterCfgUUID), (u8*)(dbgLogCCC), &app_dbgCccWrite, NULL},
	{0, ATT_PERMISSIONS_READ, 2, sizeof(PROP_WRITE), (u8*)(&my_characterUUID), (u8*)(&PROP_WRITE), NULL, NULL},
	{0, ATT_PERMISSIONS_RDWR, 16, sizeof(dbgCmdVal), (u8*)(my_dbgCmdUUID), (&dbgCmdVal), &app_dbgCmdWrite, NULL},
};

/* service mode only (never deep sleeps): outside the retention area */
_attribute_custom_bss_ u8 blt_rxfifo_b[RX_FIFO_SIZE * RX_FIFO_NUM];
_attribute_data_retention_ my_fifo_t blt_rxfifo = {RX_FIFO_SIZE, RX_FIFO_NUM, 0, 0, blt_rxfifo_b};
_attribute_custom_bss_ u8 blt_txfifo_b[TX_FIFO_SIZE * TX_FIFO_NUM];
_attribute_data_retention_ my_fifo_t blt_txfifo = {TX_FIFO_SIZE, TX_FIFO_NUM, 0, 0, blt_txfifo_b};

u8 g_ble_txPowerSet = BLE_DEFAULT_TX_POWER_IDX;
int device_in_connection_state;

static char hexDigit(u8 v)
{
	v &= 0x0f;
	return (v < 10) ? ('0' + v) : ('A' + v - 10);
}

/**********************************************************************
 * Connection callbacks
 */
static void app_startAdv(void)
{
	bls_ll_setAdvParam(DEF_ADV_INTERVAL_MIN, DEF_ADV_INTERVAL_MAX,
						ADV_TYPE_CONNECTABLE_UNDIRECTED, OWN_ADDRESS_PUBLIC,
						0, NULL,
						DEF_APP_ADV_CHANNEL,
						ADV_FP_NONE);
	bls_ll_setAdvEnable(BLC_ADV_ENABLE);
}

static void ble_connect(u8 e, u8 *p, int n)
{
	device_in_connection_state = 1;
}

static void ble_terminate(u8 e, u8 *p, int n)
{
	device_in_connection_state = 0;
	dbgLogCCC[0] = dbgLogCCC[1] = 0;
	app_startAdv();
}

static void user_set_rf_power(u8 e, u8 *p, int n)
{
	rf_set_power_level_index(g_ble_txPowerSet);
}

/* public address = low 6 bytes of the IEEE address at CFG_MAC_ADDRESS */
static void app_initMacAddress(void)
{
	u8 mac_read[8];
	u8 ff[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	flash_read_page(CFG_MAC_ADDRESS, 8, mac_read);
	if(memcmp(mac_read, ff, sizeof(mac_read))){
		memcpy(mac_public, mac_read, 6);
	}else{
		/* no address in flash: never write the MAC sector, make one up */
		generateRandomNum(3, mac_public);
		mac_public[3] = 0x38;
		mac_public[4] = 0xC1;
		mac_public[5] = 0xA4;
	}
}

static void app_initStrings(void)
{
	u8 i;
	u32 a = ota_runningAddr;

	memcpy(devName, DEV_NAME_PREFIX, sizeof(DEV_NAME_PREFIX) - 1);
	i = sizeof(DEV_NAME_PREFIX) - 1;
	devName[i++] = hexDigit(mac_public[1] >> 4);
	devName[i++] = hexDigit(mac_public[1]);
	devName[i++] = hexDigit(mac_public[0] >> 4);
	devName[i++] = hexDigit(mac_public[0]);

	/* "vRR.BB @0xXXXXX" */
	memcpy(fwRev, "v00.00 @0x00000", 16);
	fwRev[1] = hexDigit(APP_RELEASE >> 4);
	fwRev[2] = hexDigit(APP_RELEASE);
	fwRev[4] = hexDigit(APP_BUILD >> 4);
	fwRev[5] = hexDigit(APP_BUILD);
	for(i = 0; i < 5; i++){
		fwRev[14 - i] = hexDigit(a);
		a >>= 4;
	}
}

static void app_setAdvData(void)
{
	/* adv: flags + complete local name */
	u8 adv[3 + 2 + DEV_NAME_LEN];
	adv[0] = 0x02;
	adv[1] = 0x01;	/* flags */
	adv[2] = 0x06;	/* LE general discoverable, BR/EDR not supported */
	adv[3] = 1 + DEV_NAME_LEN;
	adv[4] = 0x09;	/* complete local name */
	memcpy(&adv[5], devName, DEV_NAME_LEN);
	bls_ll_setAdvData(adv, sizeof(adv));

	/* scan response: the OTA service UUID */
	u8 rsp[2 + 16];
	rsp[0] = 1 + 16;
	rsp[1] = 0x07;	/* complete list of 128-bit service UUIDs */
	memcpy(&rsp[2], my_OtaServiceUUID, 16);
	bls_ll_setScanRspData(rsp, sizeof(rsp));
}

/**********************************************************************
 * Init
 */
void user_ble_init(void)
{
	bls_smp_configParingSecurityInfoStorageAddr(CFG_NV_START_FOR_BLE);

	app_initMacAddress();
	app_initStrings();

	/* Controller */
	blc_ll_initBasicMCU();
	blc_ll_initStandby_module(mac_public);
	blc_ll_initAdvertising_module(mac_public);
	blc_ll_initSlaveRole_module();
	blc_ll_initPowerManagement_module();

	/* Host */
	blc_gap_peripheral_init();
	bls_att_setAttributeTable((u8 *)my_Attributes);
	blc_l2cap_register_handler(blc_l2cap_packet_receive);
	blc_att_setRxMtuSize(MTU_SIZE_SETTING);
	blc_smp_setSecurityLevel(No_Security);

	app_setAdvData();
	app_startAdv();

	user_set_rf_power(0, 0, 0);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &user_set_rf_power);
	bls_app_registerEventCallback(BLT_EV_FLAG_CONNECT, &ble_connect);
	bls_app_registerEventCallback(BLT_EV_FLAG_TERMINATE, &ble_terminate);

#if PM_ENABLE
	bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN);
#else
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
#endif
}

/**********************************************************************
 * Main loop: queued text commands, log streaming
 */
void app_ble_task(void)
{
	if(cmdPending){
		if(!strcmp(cmdBuf, "dump")){
			logCursor = log_oldest();
		}else if(!strcmp(cmdBuf, "clear")){
			log_clear();
			logCursor = log_head();
		}else{
			fp_command(cmdBuf);
		}
		cmdPending = 0;
	}

	if(!device_in_connection_state || !(dbgLogCCC[0] & 1) || ota_busy){
		return;
	}
	/* 20-byte notifications while the TX FIFO has room */
	while(blc_ll_getTxFifoNumber() < TX_FIFO_NUM - 2){
		u8 buf[20];
		u32 c = logCursor;
		int n = log_read(&c, buf, sizeof(buf));
		if(n <= 0){
			break;
		}
		if(bls_att_pushNotifyData(DBG_LOG_DP_H, buf, n) != BLE_SUCCESS){
			break;
		}
		logCursor = c;
	}
}
