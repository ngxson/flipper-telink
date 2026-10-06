#ifndef _BUZZER_H_
#define _BUZZER_H_

#include "tl_common.h"

/* Set PC1 (PWM0_N) idle, load melodies from NV */
void buzzer_init(void);
void buzzer_melodyInit(void);	/* (re)load the melody slots from NV */

/*
 * Play an RTTTL melody.
 *   rtttl:    "name:d=4,o=6,b=200:c,e,g..." (must be NUL terminated)
 *   loop:     restart when finished (alarm)
 *   maxNotes: stop after this many notes (0 = whole melody); used for previews
 * Returns FALSE if the string could not be parsed.
 */
bool buzzer_play(const char *rtttl, bool loop, u8 maxNotes);

void buzzer_stop(void);
bool buzzer_isPlaying(void);

/*
 * Melody slots 0..2 (Tuya DP103 values 0..2).
 * Stored in NV (NV_ITEM_APP_MELODY1..3), NUL terminated in RAM.
 */
const char *buzzer_getMelody(u8 slot);
char *buzzer_melodyBuf(u8 slot);	/* RAM buffer of the slot (for BLE readback) */

/* melody slot RAM buffers (exported so the BLE GATT table can point at them) */
extern char buzzer_melodyRam[MELODY_SLOTS][RTTTL_MAX_LEN];
bool buzzer_setMelody(u8 slot, const char *rtttl, u8 len); /* len excludes NUL */

#endif /* _BUZZER_H_ */
