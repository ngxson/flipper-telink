#ifndef _BLE_OTA_H_
#define _BLE_OTA_H_

/* physical flash address of the image we are running from (0 or OTA_SLOT_ADDR) */
extern u32 ota_runningAddr;
/* set by the OTA start command, the device reboots when the OTA ends */
extern volatile u8 ota_busy;

void app_ota_init(void);
void app_ota_task(void);

#endif
