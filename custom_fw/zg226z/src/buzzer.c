/**********************************************************************
 * Buzzer (PC1 = PWM0_N) with an RTTTL player and NV melody slots.
 *
 * The PWM tick clock is set to 1 MHz, so a note of frequency f is
 * cycle = 1000000 / f ticks. The duty cycle is deliberately low
 * ("volume: low") - the stock device has no real volume control either.
 **********************************************************************/
#include "tl_common.h"
#include "app_cfg.h"
#include "board.h"
#include "buzzer.h"
#include "zcl_include.h"

/* PWM tick clock */
#define BUZZER_PWM_HZ			1000000
#define BUZZER_DUTY_TICKS		1		/* normal notes: 1-tick pulse ("low") */
#define BELL_PEAK_TICKS			6		/* bell envelope attack level */
#define BELL_HOLD_STEPS			3		/* steps held at 1 tick before silence */

/* built-in defaults for the three melody slots */
static const char melodyDefault[MELODY_SLOTS][48] =
{
	"siren:d=4,o=6,b=200:c,g,c,g,c,g,c,g",
	"alarm:d=8,o=7,b=200:c,p,c,p,c,p,c,p",
	"classic:d=4,o=5,b=180:e,g,>c,e,g,>c,e",
};

/* RAM copies (NUL terminated), persisted in NV module 6.
 * Not in retention RAM (too large): reloaded from NV after every
 * deep-sleep wake, see buzzer_melodyInit(). */
char buzzer_melodyRam[MELODY_SLOTS][RTTTL_MAX_LEN];

/**********************************************************************
 * Tone generation
 */
static ev_timer_event_t *envTmr;
static u8 envTicks, envHold;

static void tone_start(u16 freq, u16 dutyTicks)
{
	u32 cycle;

	if(freq == 0){
		return;
	}
	if(freq < 16){
		freq = 16;	/* 16-bit cycle limit */
	}

	cycle = BUZZER_PWM_HZ / freq;
	if(cycle > 0xffff){
		cycle = 0xffff;
	}

	pwm_set_clk(CLOCK_SYS_CLOCK_HZ, BUZZER_PWM_HZ);
	/* volume: a 1-tick (1 us) pulse per cycle, ~0.1 % at 1 kHz (user-tested "low") */
	pwm_set_cycle_and_duty(BUZZER_PWM_ID, (u16)cycle, dutyTicks);
	gpio_set_func(GPIO_BUZZER, BUZZER_PWM_FUNC);
	pwm_start(BUZZER_PWM_ID);
}

static void tone_stop(void)
{
	if(envTmr){
		TL_ZB_TIMER_CANCEL(&envTmr);
	}
	pwm_stop(BUZZER_PWM_ID);
	gpio_set_func(GPIO_BUZZER, AS_GPIO);
	gpio_write(GPIO_BUZZER, 0);
}

/* Bell envelope: start at BELL_PEAK_TICKS and decay towards 1 tick with
 * growing step lengths (roughly exponential), then go silent. */
static s32 envTimerCb(void *arg)
{
	if(envTicks > 1){
		envTicks--;
		pwm_set_cmp(BUZZER_PWM_ID, envTicks);
		return (s32)(8 + (BELL_PEAK_TICKS - envTicks) * 6);
	}
	if(envHold){
		envHold--;
		return 0;	/* same period */
	}
	/* fully decayed: silence until the next note */
	envTmr = NULL;
	pwm_stop(BUZZER_PWM_ID);
	gpio_set_func(GPIO_BUZZER, AS_GPIO);
	gpio_write(GPIO_BUZZER, 0);
	return -1;
}

static void note_start(u16 freq, u8 bell)
{
	if(!bell){
		tone_start(freq, BUZZER_DUTY_TICKS);
		return;
	}
	tone_start(freq, BELL_PEAK_TICKS);
	envTicks = BELL_PEAK_TICKS;
	envHold = BELL_HOLD_STEPS;
	envTmr = TL_ZB_TIMER_SCHEDULE(envTimerCb, NULL, 8);
}

/**********************************************************************
 * RTTTL parser
 *
 * "name:d=N,o=N,b=N:note,note,..." with note = [dur][a-g|p][#|b][oct][.]
 * Supported: durations 1..32, octaves 4..7, sharps/flats, dots, pauses.
 */
typedef struct{
	const char *mel;		/* melody being played */
	u16 pos;				/* parse position */
	u8  defDur;				/* default duration (1,2,4,...) */
	u8  defOct;				/* default octave */
	u16 defBpm;				/* beats per minute (RTTTL allows > 255) */
	u8  loop;
	u8  maxNotes;			/* 0 = unlimited (previews use this) */
	u8  played;				/* notes started so far */
	u8  toneOn;				/* current phase: tone or rest */
	u16 restMs;				/* gap after the current note */
	u8  bell;				/* "e=1" in the defaults: bell-like decay per note */
	ev_timer_event_t *tmr;
} rtttlPlayer_t;

static rtttlPlayer_t player;

/* octave-4 note frequencies in Hz */
static const u16 noteFreq[12] =
{
	262,	/* c  */
	277,	/* c# */
	294,	/* d  */
	311,	/* d# */
	330,	/* e  */
	349,	/* f  */
	370,	/* f# */
	392,	/* g  */
	415,	/* g# */
	440,	/* a  */
	466,	/* a# */
	494,	/* b  */
};

static u8 rtttl_isDigit(u8 c)
{
	return (c >= '0' && c <= '9');
}

/* parse "name:defaults:notes" -> position the player at the first note */
static bool rtttl_open(rtttlPlayer_t *p, const char *mel)
{
	u16 i = 0;

	p->defDur = 4;
	p->defOct = 6;
	p->defBpm = 63;
	p->pos = 0;

	/* skip the tune name */
	while(mel[i] && mel[i] != ':'){
		i++;
	}
	if(mel[i] != ':'){
		return FALSE;
	}
	i++;

	/* defaults section: "d=N", "o=N", "b=N" separated by commas */
	while(mel[i] && mel[i] != ':'){
		u8 key = (u8)mel[i];
		u16 val = 0;
		u8 hasVal = 0;
		i++;
		if(mel[i] == '='){
			i++;
		}
		while(rtttl_isDigit((u8)mel[i])){
			val = val * 10 + (mel[i] - '0');
			i++;
			hasVal = 1;
		}
		switch(key){
			case 'd': case 'D':
				if(hasVal) p->defDur = (u8)val;
				break;
			case 'o': case 'O':
				if(hasVal) p->defOct = (u8)val;
				break;
			case 'b': case 'B':
				if(hasVal) p->defBpm = val;
				break;
			case 'e': case 'E':		/* extension: envelope (1 = bell) */
				if(hasVal) p->bell = (u8)val;
				break;
			default:
				break;
		}
		/* skip separators (never the ':' that ends the section) */
		while(mel[i] == ',' || mel[i] == ' '){
			i++;
		}
	}
	if(mel[i] != ':'){
		return FALSE;	/* no notes section */
	}
	i++;

	if(p->defDur == 0 || p->defOct < 4 || p->defOct > 8 || p->defBpm == 0){
		return FALSE;
	}
	p->pos = i;
	p->mel = mel;
	return TRUE;
}

/*
 * Parse the next note. Returns the frequency (0 = pause) and its length in ms.
 * Returns FALSE when the melody is finished.
 */
static bool rtttl_nextNote(rtttlPlayer_t *p, u16 *freq, u16 *durMs)
{
	u8 c;
	u16 dur = p->defDur;
	u8 oct = p->defOct;
	u8 dotted = 0;
	u32 ms;
	u8 note = 0xff;

	if(!p->mel[p->pos]){
		return FALSE;
	}

	/* optional duration */
	if(rtttl_isDigit((u8)p->mel[p->pos])){
		dur = 0;
		while(rtttl_isDigit((u8)p->mel[p->pos])){
			dur = dur * 10 + (p->mel[p->pos] - '0');
			p->pos++;
		}
		if(dur == 0){
			return FALSE;
		}
	}

	/* optional octave shifts: '>' raises, '<' lowers the following note */
	while(p->mel[p->pos] == '>' || p->mel[p->pos] == '<'){
		if(p->mel[p->pos] == '>'){
			oct = (oct < 7) ? (oct + 1) : 7;
		}else{
			oct = (oct > 4) ? (oct - 1) : 4;
		}
		p->pos++;
	}

	/* pitch */
	c = p->mel[p->pos++];
	switch(c){
		case 'p': case 'P':
			note = 0xfe;	/* pause */
			break;
		case 'a': case 'A': note = 9;  break;
		case 'b': case 'B': note = 11; break;
		case 'c': case 'C': note = 0;  break;
		case 'd': case 'D': note = 2;  break;
		case 'e': case 'E': note = 4;  break;
		case 'f': case 'F': note = 5;  break;
		case 'g': case 'G': note = 7;  break;
		default:
			return FALSE;
	}

	if(note != 0xfe && (p->mel[p->pos] == '#' || p->mel[p->pos] == 'b' || p->mel[p->pos] == 'B')){
		u8 acc = (u8)p->mel[p->pos++];
		if(acc == '#' && note < 11){
			note++;
		}else if((acc == 'b' || acc == 'B') && note > 0){
			note--;
		}
	}

	/* optional octave */
	if(rtttl_isDigit((u8)p->mel[p->pos])){
		oct = (u8)(p->mel[p->pos] - '0');
		p->pos++;
	}

	/* optional dot */
	if(p->mel[p->pos] == '.'){
		dotted = 1;
		p->pos++;
	}

	/* skip separators */
	while(p->mel[p->pos] == ',' || p->mel[p->pos] == ' '){
		p->pos++;
	}

	/* duration in ms: (60 / bpm) * 4 / dur seconds */
	ms = (u32)(240000 / p->defBpm) / dur;
	if(ms < 10){
		ms = 10;
	}
	if(ms > 4000){
		ms = 4000;
	}
	if(dotted){
		ms = ms * 3 / 2;
	}
	*durMs = (u16)ms;

	if(note == 0xfe){
		*freq = 0;
	}else{
		if(oct < 4){
			oct = 4;
		}
		if(oct > 7){
			oct = 7;
		}
		*freq = (u16)(noteFreq[note] << (oct - 4));
	}

	return TRUE;
}

/* fetch the next note, restarting the melody if looping */
static bool player_next(u16 *freq, u16 *dur)
{
	if(!rtttl_nextNote(&player, freq, dur)){
		if(player.loop && rtttl_open(&player, player.mel)){
			player.played = 0;
			if(!rtttl_nextNote(&player, freq, dur)){
				return FALSE;
			}
		}else{
			return FALSE;
		}
	}
	return TRUE;
}

static s32 playerTimerCb(void *arg)
{
	u16 freq, dur;

	if(player.toneOn){
		/* tone phase over: silence for the rest of the note */
		tone_stop();
		player.toneOn = 0;
		if(player.restMs){
			return (s32)player.restMs;
		}
	}

	{
		if(player.maxNotes && player.played >= player.maxNotes){
			player.tmr = NULL;
			return -1;
		}
		if(!player_next(&freq, &dur)){
			player.tmr = NULL;
			return -1;
		}

		player.played++;
		player.toneOn = 1;
		player.restMs = dur / 5;
		if(freq){
			note_start(freq, player.bell);
		}
		return (s32)(dur - player.restMs);
	}
}

/**********************************************************************
 * Public API
 */
void buzzer_init(void)
{
	memset((u8 *)&player, 0, sizeof(player));
	tone_stop();
	buzzer_melodyInit();
}

bool buzzer_play(const char *rtttl, bool loop, u8 maxNotes)
{
	u16 freq, dur;

	buzzer_stop();

	if(!rtttl_open(&player, rtttl)){
		memset((u8 *)&player, 0, sizeof(player));
		return FALSE;
	}

	player.loop = loop ? 1 : 0;
	player.maxNotes = maxNotes;

	if(!player_next(&freq, &dur)){
		memset((u8 *)&player, 0, sizeof(player));
		return FALSE;
	}

	player.played = 1;
	player.toneOn = 1;
	if(freq){
		note_start(freq, player.bell);
	}
	player.restMs = dur / 5;
	player.tmr = TL_ZB_TIMER_SCHEDULE(playerTimerCb, NULL, (u32)(dur - player.restMs));
	return TRUE;
}

void buzzer_stop(void)
{
	if(player.tmr){
		TL_ZB_TIMER_CANCEL(&player.tmr);
	}
	tone_stop();
	memset((u8 *)&player, 0, sizeof(player));
}

bool buzzer_isPlaying(void)
{
	return player.tmr != NULL;
}

/**********************************************************************
 * Melody slots
 */
void buzzer_melodyInit(void)
{
	u8 slot;

	for(slot = 0; slot < MELODY_SLOTS; slot++){
		u8 itemId = NV_ITEM_APP_MELODY1 + slot;
		u16 len = 0;
		bool ok = FALSE;

		if(nv_flashSingleItemSizeGet(NV_MODULE_APP, itemId, &len) == NV_SUCC
			&& len >= 2 && len < RTTTL_MAX_LEN
			&& nv_flashReadNew(1, NV_MODULE_APP, itemId, len, (u8 *)buzzer_melodyRam[slot]) == NV_SUCC){
			rtttlPlayer_t t;
			u16 f, d;
			buzzer_melodyRam[slot][len] = 0;
			if(rtttl_open(&t, buzzer_melodyRam[slot]) && rtttl_nextNote(&t, &f, &d)){
				ok = TRUE;
			}
		}

		if(!ok){
			u16 dl = (u16)strlen(melodyDefault[slot]);
			memcpy(buzzer_melodyRam[slot], melodyDefault[slot], dl);
			buzzer_melodyRam[slot][dl] = 0;
		}
	}
}

const char *buzzer_getMelody(u8 slot)
{
	if(slot >= MELODY_SLOTS){
		return melodyDefault[0];
	}
	return buzzer_melodyRam[slot];
}

char *buzzer_melodyBuf(u8 slot)
{
	if(slot >= MELODY_SLOTS){
		return NULL;
	}
	return buzzer_melodyRam[slot];
}

bool buzzer_setMelody(u8 slot, const char *rtttl, u8 len)
{
	rtttlPlayer_t t;
	u16 f, d;

	if(slot >= MELODY_SLOTS || len == 0 || len >= RTTTL_MAX_LEN){
		return FALSE;
	}

	/* validate before touching NV */
	if(!rtttl_open(&t, rtttl) || !rtttl_nextNote(&t, &f, &d)){
		return FALSE;
	}

	/* stop anything playing (it may be the melody we replace) */
	buzzer_stop();

	memcpy(buzzer_melodyRam[slot], rtttl, len);
	buzzer_melodyRam[slot][len] = 0;

	return nv_flashWriteNew(1, NV_MODULE_APP, NV_ITEM_APP_MELODY1 + slot, len, (u8 *)buzzer_melodyRam[slot]) == NV_SUCC;
}
