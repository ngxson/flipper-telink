#ifndef _FP_H_
#define _FP_H_

/* HLK-ZW101 fingerprint module on UART PB1 (TX) / PB7 (RX), 57600 8N1.
 * VCC switched on PD4 (active high, see board.h), TOUCH_OUT on PD2. */

void fp_init(void);
void fp_task(void);				/* main loop */
bool fp_busy(void);				/* module powered: keep the CPU out of suspend */
void fp_command(const char *s);	/* text command from BLE, see fp.c */
void fp_enroll(void);			/* button 1 */
void fp_clearAll(void);			/* button 2 */

_attribute_ram_code_ void fp_uartIrq(void);

#endif
