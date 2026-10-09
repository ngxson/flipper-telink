#ifndef _ZB_DEV_H_
#define _ZB_DEV_H_

/* Zigbee mode: a Tuya TS0044 (4 buttons) / TS0046 (6 buttons) scene
 * switch as Zigbee2MQTT knows it - see zb_dev.c */

void zb_main(u8 isRetention);			/* never returns */
void zb_buttonEvent(int btn, u8 ev);	/* buttons.c: BTN_EV_* */
_attribute_ram_code_ void zb_irqHandler(void);

#endif
