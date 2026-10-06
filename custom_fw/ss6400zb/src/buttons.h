#ifndef _BUTTONS_H_
#define _BUTTONS_H_

/* the 4 buttons of the remote (board.h BUTTON1..4, active low) */
void btn_init(void);
void btn_task(void);	/* main loop: debounced presses -> actions */

#endif
