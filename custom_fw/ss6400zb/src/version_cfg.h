#ifndef _VERSION_CFG_H_
#define _VERSION_CFG_H_

#define BOOT_LOADER_MODE				0
#define BOOT_LOADER_IMAGE_ADDR			0x0
#define APP_IMAGE_ADDR					0x0

#define BOARD_SS6400ZB					0x31

#ifndef BOARD
#define BOARD							BOARD_SS6400ZB
#endif

#define TLSR_8258_1M					0x03
#define CHIP_TYPE						TLSR_8258_1M

/* bump APP_RELEASE/APP_BUILD for every image pushed over BLE so the
 * firmware revision characteristic shows which one is running */
#define APP_RELEASE						0x00
#ifndef APP_BUILD
#define APP_BUILD						0x0E
#endif
#define STACK_RELEASE					0x30
#define STACK_BUILD						0x01

#define MANUFACTURER_CODE_TELINK		0x1141
#define IMAGE_TYPE						((CHIP_TYPE << 8) | BOARD)
#define FILE_VERSION					((APP_RELEASE << 24) | (APP_BUILD << 16) | (STACK_RELEASE << 8) | STACK_BUILD)

#define IS_BOOT_LOADER_IMAGE			0
#define RESV_FOR_APP_RAM_CODE_SIZE		0
#define IMAGE_OFFSET					APP_IMAGE_ADDR

#endif
