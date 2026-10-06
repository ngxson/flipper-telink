#ifndef _BOARD_H_
#define _BOARD_H_
/* HOBEIAN ZG-226Z / ZG-228Z vibration alarm (TLSR8258, 512K flash)
 * PA0 vibration switch, PA7 SWS, PB1 TX pad (BLE mode switch: bridged to GND),
 * PB7 RX pad, PC1 buzzer (PWM0), PD3 button, PD4 LED (PWM2_N, dimmed) */

#define BUTTON1					GPIO_PD3
#define PD3_FUNC				AS_GPIO
#define PD3_OUTPUT_ENABLE		0
#define PD3_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PD3		PM_PIN_PULLUP_10K

#define GPIO_VIBRATION			GPIO_PA0
#define PA0_FUNC				AS_GPIO
#define PA0_OUTPUT_ENABLE		0
#define PA0_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PA0		PM_PIN_PULLUP_1M

/* BLE mode switch: TX pad (PB1) bridged to RX pad (PB7).
 * PB1 idles as an output driven high, PB7 is an input with pull-up;
 * the firmware toggles PB1 and checks that PB7 follows. */
#define GPIO_BRIDGE_TX			GPIO_PB1
#define PB1_FUNC				AS_GPIO
#define PB1_OUTPUT_ENABLE		1
#define PB1_INPUT_ENABLE		0
#define PB1_DATA_OUT			1
#define GPIO_BRIDGE_RX			GPIO_PB7
#define PB7_FUNC				AS_GPIO
#define PB7_OUTPUT_ENABLE		0
#define PB7_INPUT_ENABLE		1
#define PULL_WAKEUP_SRC_PB7		PM_PIN_PULLUP_10K

#define LED_ON					1
#define LED_OFF					0
#define GPIO_LED				GPIO_PD4
#define PD4_FUNC				AS_GPIO
#define PD4_OUTPUT_ENABLE		1
#define PD4_INPUT_ENABLE		0
#define PD4_DATA_OUT			LED_OFF

#define GPIO_BUZZER				GPIO_PC1
#define BUZZER_PWM_ID			PWM0_ID
#define BUZZER_PWM_FUNC			AS_PWM0		/* as the stock firmware */
#define PC1_FUNC				AS_GPIO
#define PC1_OUTPUT_ENABLE		1
#define PC1_INPUT_ENABLE		0
#define PC1_DATA_OUT			0

#define SHL_ADC_VBAT			C5P
#define GPIO_VBAT				GPIO_PC5
#define PC5_INPUT_ENABLE		0
#define PC5_DATA_OUT			1
#define PC5_OUTPUT_ENABLE		1
#define PC5_FUNC				AS_GPIO

#endif
