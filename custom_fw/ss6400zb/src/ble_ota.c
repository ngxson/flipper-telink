/**********************************************************************
 * BLE OTA with two image slots (0x00000 and OTA_SLOT_ADDR).
 *
 * The transfer itself is the Telink OTA protocol implemented by the BLE
 * library (otaWrite, see the attribute table in app_ble.c). It writes the
 * new image to ota_program_offset, checks its CRC32, then sets the boot
 * flag (0x4B at +8) of the new image and clears the flag of the running
 * one, and reboots. The boot ROM starts whichever slot has the flag.
 *
 * Nothing in the SDK picks the slot to write to, so do it here: the
 * running image is the one whose flag is set (flash reads are physical,
 * not remapped), the other slot is the target.
 **********************************************************************/
#include "tl_common.h"
#include "stack/ble/ble.h"
#include "ble_ota.h"

#define TLNK_FLAG_OFFSET	8
#define TLNK_FLAG			0x4B

u32 ota_runningAddr;
volatile u8 ota_busy;

static void ota_startCb(void)
{
	ota_busy = 1;
	/* stay awake: the client streams writes as fast as it can */
	bls_pm_setSuspendMask(SUSPEND_DISABLE);
	gpio_write(GPIO_LED, LED_ON);
}

static void ota_resultCb(int result)
{
	/* the library reboots right after this */
	(void)result;
	gpio_write(GPIO_LED, LED_OFF);
}

void app_ota_init(void)
{
	u8 flag = 0;

	flash_read_page(TLNK_FLAG_OFFSET, 1, &flag);
	ota_runningAddr = (flag == TLNK_FLAG) ? 0 : OTA_SLOT_ADDR;

	/* sets both the boot address and ota_program_offset to OTA_SLOT_ADDR */
	bls_ota_set_fwSize_and_fwBootAddr(OTA_SLOT_SIZE_K, OTA_SLOT_ADDR);
	if(ota_runningAddr){
		ota_program_offset = 0;
	}

	/* erase leftovers (old image, aborted transfer) in the target slot */
	bls_ota_clearNewFwDataArea();

	bls_ota_setTimeout(OTA_TIMEOUT_US);
	bls_ota_registerStartCmdCb(ota_startCb);
	bls_ota_registerResultIndicateCb(ota_resultCb);
}

void app_ota_task(void)
{
	if(blcOta.ota_start_flag){
		bls_ota_procTimeout();
	}
}
