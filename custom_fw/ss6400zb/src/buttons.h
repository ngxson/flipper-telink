#ifndef _BUTTONS_H_
#define _BUTTONS_H_

/* the 4 (TS0044) or 6 (TS0046) buttons of the remote, active low.
 * Short press fires on the release edge, long press after 1 s of hold,
 * very long after 10 s; no double-click window, so events go out with
 * no delay. */

/* events passed to the action handlers */
enum{
	BTN_EV_SHORT,		/* released before 1 s */
	BTN_EV_LONG,		/* 1 s of hold reached (still held) */
	BTN_EV_VLONG,		/* 10 s of hold reached (still held) */
	BTN_EV_VLONG_UP,	/* released after a very long press */
};

void btn_init(void);
void btn_task(void);			/* main loop: short/long/very-long state machine */
u32 btn_wakeDeadline(void);		/* PM: next hold-threshold wake tick, 0 = none */
u8 btn_anyDown(void);			/* a button is held (or not yet seen released) */
u8 btn_heldFor(int i, u32 ms);	/* button i has been down for >= ms */
u8 btn_isDown(int i);			/* raw level of button i (power-up check) */
void btn_ignoreUntilRelease(int i);	/* swallow the current press of button i */
void btn_armWakeAll(void);		/* before deep sleep: wake on any press */
u8 btn_count(void);				/* 4 or 6 */

#endif
