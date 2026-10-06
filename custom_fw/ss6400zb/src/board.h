#ifndef _BOARD_H_
#define _BOARD_H_
/* ZT3L module in the SS6400zb / Tuya TS0044 4-button remote (TLSR8258, 1 MB).
 * Pins from the stock factory config, see ../README.md.
 * PA0/PB5/PC2/PC3 buttons (active low), PD7 network LED (active high), PA7 SWS */

#define LED_ON					1
#define LED_OFF					0
#define GPIO_LED				GPIO_PD7
#define PD7_FUNC				AS_GPIO
#define PD7_OUTPUT_ENABLE		1
#define PD7_INPUT_ENABLE		0
#define PD7_DATA_OUT			LED_OFF

/* buttons: not used yet, kept as inputs with pull-ups so they don't float */
#define BUTTON1					GPIO_PA0
#define PA0_FUNC				AS_GPIO
#define PA0_OUTPUT_ENABLE		0
#define PA0_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PA0		PM_PIN_PULLUP_10K

#define BUTTON2					GPIO_PB5
#define PB5_FUNC				AS_GPIO
#define PB5_OUTPUT_ENABLE		0
#define PB5_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PB5		PM_PIN_PULLUP_10K

#define BUTTON3					GPIO_PC2
#define PC2_FUNC				AS_GPIO
#define PC2_OUTPUT_ENABLE		0
#define PC2_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PC2		PM_PIN_PULLUP_10K

#define BUTTON4					GPIO_PC3
#define PC3_FUNC				AS_GPIO
#define PC3_OUTPUT_ENABLE		0
#define PC3_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PC3		PM_PIN_PULLUP_10K

/* HLK-ZW101 fingerprint module (pads: TX=L1, RX=L2, D4=L6, D2=R5)
 * UART PB1 -> module RX, PB7 <- module TX: floating inputs while the module
 * is unpowered (fp.c attaches the UART on power-up) */
#define GPIO_FP_TX				GPIO_PB1
#define PB1_FUNC				AS_GPIO
#define PB1_OUTPUT_ENABLE		0
#define PB1_INPUT_ENABLE		0
#define GPIO_FP_RX				GPIO_PB7
#define PB7_FUNC				AS_GPIO
#define PB7_OUTPUT_ENABLE		0
#define PB7_INPUT_ENABLE		0

/* module VCC switch on PD4. Measured on the real wiring (fp diag): PD4
 * HIGH powers the module, so the switch is active high */
#define GPIO_FP_PWR				GPIO_PD4
#define FP_PWR_ON				1
#define FP_PWR_OFF				0
#define PD4_FUNC				AS_GPIO
#define PD4_OUTPUT_ENABLE		1
#define PD4_INPUT_ENABLE		0
#define PD4_DATA_OUT			FP_PWR_OFF

/* TOUCH_OUT: high while a finger is on the sensor; wake source */
#define GPIO_FP_TOUCH			GPIO_PD2
#define PD2_FUNC				AS_GPIO
#define PD2_OUTPUT_ENABLE		0
#define PD2_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PD2		PM_PIN_PULLDOWN_100K

#endif
