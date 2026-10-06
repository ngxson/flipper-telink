#ifndef _VERSION_CFG_H_
#define _VERSION_CFG_H_

#define BOOT_LOADER_MODE				0
#define BOOT_LOADER_IMAGE_ADDR			0x0
#define APP_IMAGE_ADDR					0x0

#define BOARD_ZG228Z					0x30

#ifndef BOARD
#define BOARD							BOARD_ZG228Z
#endif

#define TLSR_8258_512K					0x02
#define CHIP_TYPE						TLSR_8258_512K

#define APP_RELEASE						0x01
#define APP_BUILD						0x00
#define STACK_RELEASE					0x30
#define STACK_BUILD						0x01

#define MANUFACTURER_CODE_TELINK		0x1141
#define IMAGE_TYPE						((CHIP_TYPE << 8) | BOARD)
#define FILE_VERSION					((APP_RELEASE << 24) | (APP_BUILD << 16) | (STACK_RELEASE << 8) | STACK_BUILD)

#define IS_BOOT_LOADER_IMAGE			0
#define RESV_FOR_APP_RAM_CODE_SIZE		0
#define IMAGE_OFFSET					APP_IMAGE_ADDR

#endif
