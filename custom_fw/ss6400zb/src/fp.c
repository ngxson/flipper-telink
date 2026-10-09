/**********************************************************************
 * HLK-ZW101 fingerprint module driver (EF01 protocol) + test jobs.
 *
 * Packet: EF 01 | addr FF FF FF FF | PID | LEN (BE, payload + 2) |
 *         payload | SUM (BE, 16-bit sum of PID, LEN bytes and payload)
 * PID 0x01 = command, 0x07 = reply (payload[0] = confirmation code).
 *
 * Power: the module's VCC is only on while a job runs (it draws mA even
 * asleep); V_SENSOR stays on so TOUCH_OUT (PD2, high = finger) can wake
 * us. While VCC is off, TX/RX are floating inputs so they don't feed the
 * module through its IO diodes.
 *
 * Jobs (started by a touch, a button or a BLE text command):
 *   scan   - touch: capture, extract, search 1:N -> green 1 s match / red 1 s
 *   enroll - button 1: LED blinks blue, 2 presses, store at the next id ->
 *            green 1 s / red 1 s
 *   clear  - button 2: delete all templates -> yellow 1 s / red 1 s
 *   info   - handshake, check sensor, system parameters, template count
 *   raw    - one command given as hex payload, module stays on 10 s
 **********************************************************************/
#include "tl_common.h"
#include "stack/ble/ble.h"
#include "fp.h"
#include "log.h"

extern void led_blip(void);

#define FP_BAUD					57600
#define FP_BOOT_MS				1000	/* max wait for the 0x55 "ready" byte */
#define FP_REPLY_MS				1500
#define FP_SCAN_WAIT_MS			2000	/* scan: wait for a finger this long */
#define FP_ENROLL_MS			20000	/* enroll: whole procedure */
#define FP_RESULT_MS			1000	/* result colour shown this long */
#define FP_ENROLL_PRESSES		2		/* GenChar buffers 1..N, then RegModel */
#define FP_RAW_KEEP_MS			10000	/* raw: keep powered after the last command */
#define FP_SEARCH_PAGES			50		/* library size (ReadSysPara) */

/* 1: a touch powers the module and runs a scan. 0: a touch only counts
 * (e.g. on a weak coin cell the module can't boot, see README) */
#ifndef FP_SCAN_ON_TOUCH
#define FP_SCAN_ON_TOUCH		1
#endif

#define CMD_GET_IMAGE			0x01
#define CMD_GEN_CHAR			0x02
#define CMD_SEARCH				0x04
#define CMD_REG_MODEL			0x05
#define CMD_STORE_CHAR			0x06
#define CMD_EMPTY				0x0D
#define CMD_READ_SYS_PARA		0x0F
#define CMD_VALID_TEMPLATE_NUM	0x1D
#define CMD_HANDSHAKE			0x35
#define CMD_CHECK_SENSOR		0x36
#define CMD_CONTROL_BLN			0x3C	/* LED: mode, start colour, end colour, cycles */

/* PS_ControlBLN modes and colour bits (verified: all accepted by the ZW101) */
#define BLN_BREATHE				0x01
#define BLN_FLASH				0x02
#define BLN_ON					0x03
#define BLN_OFF					0x04
#define LED_BLUE				0x01
#define LED_GREEN				0x02
#define LED_RED					0x04

typedef enum{
	FP_OFF,
	FP_BOOT,		/* VCC on, waiting for 0x55 or FP_BOOT_MS */
	FP_READY,		/* run the next job step */
	FP_WAIT,		/* waiting for a reply */
	FP_DELAY,		/* job step delay */
	FP_HOLD,		/* raw job done, staying on */
} fp_state_e;

typedef enum{
	JOB_NONE,
	JOB_INFO,
	JOB_SCAN,		/* identify: green 1 s on a match, red 1 s otherwise */
	JOB_ENROLL,		/* blue blinking, FP_ENROLL_PRESSES captures, store at the next id */
	JOB_CLEAR,		/* delete every template */
	JOB_RAW,
} fp_job_e;

/* job steps (each sends one command; the reply picks the next step) */
enum{
	ST_HANDSHAKE, ST_CHECK_SENSOR, ST_SYS_PARA, ST_TEMPLATES,	/* info */
	ST_ENROLL_LED, ST_ENROLL_COUNT,							/* enroll setup */
	ST_GET_IMAGE, ST_GEN_CHAR, ST_SEARCH,					/* scan; enroll capture */
	ST_WAIT_LIFT, ST_REG_MODEL, ST_STORE,					/* enroll */
	ST_EMPTY,												/* clear */
	ST_RAW,
	ST_SHOW,		/* show resultColor for FP_RESULT_MS, then finish */
	ST_DONE,
};

static fp_state_e state;
static fp_job_e job;
static u8 step;
static u32 stateTick, jobTick, delayMs;
static u8 touchArmed;
static u32 touchCount;
static u8 manualOn;

/* RX ring, filled by the UART irq */
#define RX_RING		256
static volatile _attribute_custom_bss_ u8 rxRing[RX_RING];	/* service mode only */
static volatile u16 rxHead;
static u16 rxTail;
static volatile u16 rxErr;
static u16 rxStart;		/* rxHead at power-on: rxHead - rxStart = bytes received */
static u8 retries;
static u8 ledOffPending;	/* the module lights its LED at power-on: switch it off first */
static u8 rxIdx;
static u16 rxIdle;
static volatile u8 rxStuck;

/* reply parser */
#define PKT_MAX		64
static u8 pkt[9 + PKT_MAX + 2];
static u16 pktPos, pktLen;
static u8 reply[PKT_MAX];		/* payload of the last reply */
static u16 replyLen;
static u8 lastCmd;

static u8 resultColor;		/* ST_SHOW */
static u8 sawFinger;		/* scan: a capture saw a finger (even a bad one) */
static u8 enrollPress;		/* 1..FP_ENROLL_PRESSES */
static u16 enrollId;

static u8 rawCmd[16];
static u8 rawLen;

/**********************************************************************
 * UART + power
 */
_attribute_ram_code_ void fp_uartIrq(void)
{
	if(reg_uart_status0 & FLD_UART_RX_ERR_FLAG){
		reg_uart_status0 = FLD_UART_CLEAR_RX_FLAG;
		rxErr++;
	}
	u8 n = reg_uart_buf_cnt & FLD_UART_RX_BUF_CNT;
	if(!n){
		/* irq without data: never let a stuck flag starve the BLE stack */
		if(++rxIdle > 1000){
			reg_irq_mask &= ~FLD_IRQ_UART_EN;
			rxStuck = 1;
		}
		return;
	}
	rxIdle = 0;
	while(n--){
		rxRing[rxHead % RX_RING] = reg_uart_data_buf(rxIdx);
		rxIdx = (rxIdx + 1) & 3;
		rxHead++;
	}
}

static void fp_uartOn(void)
{
	reg_clk_en0 |= FLD_CLK0_UART_EN;
	uart_reset();
	uart_ndma_clear_tx_index();
	rxIdx = 0;
	rxIdle = 0;
	rxTail = rxHead;
	rxStart = rxHead;
	uart_gpio_set(UART_TX_PB1, UART_RX_PB7);
	uart_init_baudrate(FP_BAUD, CLOCK_SYS_CLOCK_HZ, PARITY_NONE, STOP_BIT_ONE);
	uart_dma_enable(0, 0);
	uart_ndma_irq_triglevel(1, 0);
	uart_irq_enable(1, 0);
	reg_irq_mask |= FLD_IRQ_UART_EN;
}

static void fp_uartOff(void)
{
	reg_irq_mask &= ~FLD_IRQ_UART_EN;
	uart_irq_enable(0, 0);
	/* floating inputs: nothing may back-power the module */
	gpio_set_func(GPIO_FP_TX, AS_GPIO);
	gpio_set_func(GPIO_FP_RX, AS_GPIO);
	gpio_set_output_en(GPIO_FP_TX, 0);
	gpio_set_output_en(GPIO_FP_RX, 0);
	gpio_set_input_en(GPIO_FP_TX, 0);
	gpio_set_input_en(GPIO_FP_RX, 0);
	gpio_setup_up_down_resistor(GPIO_FP_TX, PM_PIN_UP_DOWN_FLOAT);
	gpio_setup_up_down_resistor(GPIO_FP_RX, PM_PIN_UP_DOWN_FLOAT);
	reg_clk_en0 &= ~FLD_CLK0_UART_EN;
}

static void fp_power(bool on)
{
	if(on){
		gpio_write(GPIO_FP_PWR, FP_PWR_ON);
		fp_uartOn();
		ledOffPending = 1;
	}else{
		fp_uartOff();
		gpio_write(GPIO_FP_PWR, FP_PWR_OFF);
	}
}

static void fp_send(const u8 *payload, u8 len)
{
	u8 hdr[9] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0, len + 2};
	u16 sum = 0x01 + len + 2;
	int i;

	for(i = 0; i < len; i++){
		sum += payload[i];
	}
	for(i = 0; i < 9; i++){
		uart_ndma_send_byte(hdr[i]);
	}
	for(i = 0; i < len; i++){
		uart_ndma_send_byte(payload[i]);
	}
	uart_ndma_send_byte(sum >> 8);
	uart_ndma_send_byte(sum & 0xff);

	lastCmd = payload[0];
	pktPos = 0;
	log_hex("tx", payload, len);
}

static void fp_cmd0(u8 cmd)
{
	fp_send(&cmd, 1);
}

/* feed RX bytes to the packet parser; returns 1 when a reply is complete */
static int fp_parse(void)
{
	while(rxTail != rxHead){
		u8 b = rxRing[rxTail % RX_RING];
		rxTail++;

		if(pktPos == 0 && b != 0xEF){
			if(b == 0x55 && state == FP_BOOT){
				return 2;	/* power-on "ready" byte */
			}
			log_printf("rx stray %02X", b);
			continue;
		}
		if(pktPos == 1 && b != 0x01){
			pktPos = (b == 0xEF) ? 1 : 0;
			continue;
		}
		if(pktPos < sizeof(pkt)){
			pkt[pktPos] = b;
		}
		pktPos++;
		if(pktPos == 9){
			pktLen = (pkt[7] << 8) | pkt[8];
			if(pktLen < 3 || pktLen > PKT_MAX + 2){
				log_printf("bad reply len %u", pktLen);
				pktPos = 0;
			}
		}else if(pktPos > 9 && pktPos == 9 + pktLen){
			u16 sum = pkt[6] + pkt[7] + pkt[8];
			u16 n = pktLen - 2;
			for(u16 i = 0; i < n; i++){
				sum += pkt[9 + i];
			}
			u16 got = (pkt[9 + n] << 8) | pkt[10 + n];
			pktPos = 0;
			if(sum != got){
				log_printf("reply checksum %04X != %04X", got, sum);
				continue;
			}
			if(pkt[6] != 0x07){
				log_printf("pid %02X ignored", pkt[6]);
				continue;
			}
			memcpy(reply, &pkt[9], n);
			replyLen = n;
			return 1;
		}
	}
	return 0;
}

static const char *fp_codeStr(u8 c)
{
	switch(c){
		case 0x00: return "ok";
		case 0x01: return "packet error";
		case 0x02: return "no finger";
		case 0x03: return "capture failed";
		case 0x06: return "image too messy";
		case 0x07: return "too few features";
		case 0x09: return "no match";
		case 0x0A: return "merge failed";
		case 0x0B: return "id out of range";
		case 0x17: return "residual finger";
		case 0x18: return "flash error";
		case 0x1F: return "library full";
		case 0x26: return "timeout";
		case 0x29: return "sensor error";
		default: return "?";
	}
}

/**********************************************************************
 * Jobs
 */
static void fp_finish(void)
{
	if(job == JOB_RAW || manualOn){
		state = FP_HOLD;
		stateTick = clock_time();
		job = JOB_NONE;
		return;
	}
	fp_power(0);	/* also switches the module LED off */
	log_printf("fp off (%u ms)", (clock_time() - jobTick) / (1000 * sysTimerPerUs));
	state = FP_OFF;
	job = JOB_NONE;
}

static void fp_start(fp_job_e j)
{
	if(job != JOB_NONE){
		log_printf("busy");
		return;
	}
	static const u8 first[] = {
		[JOB_INFO] = ST_HANDSHAKE,
		[JOB_SCAN] = ST_GET_IMAGE,
		[JOB_ENROLL] = ST_ENROLL_LED,
		[JOB_CLEAR] = ST_EMPTY,
		[JOB_RAW] = ST_RAW,
	};
	job = j;
	step = first[j];
	retries = 0;
	sawFinger = 0;
	jobTick = clock_time();
	if(state == FP_OFF){
		log_printf("fp on");
		fp_power(1);
		state = FP_BOOT;
		stateTick = clock_time();
	}else{
		state = FP_READY;
	}
}

static void fp_delay(u32 ms)
{
	delayMs = ms;
	stateTick = clock_time();
	state = FP_DELAY;
}

static void fp_led(u8 mode, u8 color)
{
	u8 c[5] = {CMD_CONTROL_BLN, mode, color, color, 0};
	fp_send(c, sizeof(c));
}

/* end the job with a colour shown for FP_RESULT_MS */
static void fp_result(u8 color)
{
	resultColor = color;
	step = ST_SHOW;
	state = FP_READY;
}

/* send the command of `step`; called in FP_READY */
static void fp_jobStep(void)
{
	u8 c[8];

	if(ledOffPending){
		/* before the job's first step; the reply doesn't advance `step` */
		fp_led(BLN_OFF, 0);
		state = FP_WAIT;
		stateTick = clock_time();
		return;
	}

	switch(step){
		case ST_HANDSHAKE:		fp_cmd0(CMD_HANDSHAKE); break;
		case ST_CHECK_SENSOR:	fp_cmd0(CMD_CHECK_SENSOR); break;
		case ST_SYS_PARA:		fp_cmd0(CMD_READ_SYS_PARA); break;
		case ST_TEMPLATES:
		case ST_ENROLL_COUNT:	fp_cmd0(CMD_VALID_TEMPLATE_NUM); break;
		case ST_ENROLL_LED:		fp_led(BLN_FLASH, LED_BLUE); break;
		case ST_GET_IMAGE:
		case ST_WAIT_LIFT:		fp_cmd0(CMD_GET_IMAGE); break;
		case ST_GEN_CHAR:
			c[0] = CMD_GEN_CHAR;
			c[1] = (job == JOB_ENROLL) ? enrollPress : 1;
			fp_send(c, 2);
			break;
		case ST_SEARCH:
			c[0] = CMD_SEARCH; c[1] = 1;
			c[2] = 0; c[3] = 0;						/* start page */
			c[4] = 0; c[5] = FP_SEARCH_PAGES;		/* page count */
			fp_send(c, 6);
			break;
		case ST_REG_MODEL:		fp_cmd0(CMD_REG_MODEL); break;
		case ST_STORE:
			c[0] = CMD_STORE_CHAR; c[1] = 1;
			c[2] = enrollId >> 8; c[3] = enrollId & 0xff;
			fp_send(c, 4);
			break;
		case ST_EMPTY:			fp_cmd0(CMD_EMPTY); break;
		case ST_RAW:			fp_send(rawCmd, rawLen); break;
		case ST_SHOW:
			fp_led(BLN_ON, resultColor);
			break;
		default:
			fp_finish();
			return;
	}
	state = FP_WAIT;
	stateTick = clock_time();
}

static void fp_logReply(u8 code, const u8 *d)
{
	if(lastCmd == CMD_READ_SYS_PARA && code == 0 && replyLen >= 17){
		log_printf("sys: status %04X id %04X lib %u level %u addr %02X%02X%02X%02X pkt %u baud %u",
				(d[0] << 8) | d[1], (d[2] << 8) | d[3], (d[4] << 8) | d[5], (d[6] << 8) | d[7],
				d[8], d[9], d[10], d[11], 32 << ((d[12] << 8) | d[13]), 9600 * ((d[14] << 8) | d[15]));
	}else if(lastCmd == CMD_VALID_TEMPLATE_NUM && code == 0 && replyLen >= 3){
		log_printf("templates: %u", (d[0] << 8) | d[1]);
	}else if(lastCmd == CMD_SEARCH && code == 0 && replyLen >= 5){
		log_printf("MATCH id %u score %u", (d[0] << 8) | d[1], (d[2] << 8) | d[3]);
	}else if(lastCmd == CMD_GET_IMAGE && code == 0x02){
		/* "no finger" while polling: too chatty to log */
	}else{
		log_printf("rx %02X: %02X %s", lastCmd, code, fp_codeStr(code));
		if(replyLen > 1){
			log_hex("   data", d, replyLen - 1);
		}
	}
}

/* handle the reply to `step` and pick the next one */
static void fp_jobReply(void)
{
	u8 code = reply[0];
	const u8 *d = &reply[1];

	if(lastCmd == CMD_CONTROL_BLN && ledOffPending){
		ledOffPending = 0;
		if(code){
			log_printf("led off: %02X %s", code, fp_codeStr(code));
		}
		state = FP_READY;
		return;
	}

	fp_logReply(code, d);
	state = FP_READY;

	switch(step){
		/* info */
		case ST_HANDSHAKE:		step = ST_CHECK_SENSOR; break;
		case ST_CHECK_SENSOR:	step = ST_SYS_PARA; break;
		case ST_SYS_PARA:		step = ST_TEMPLATES; break;
		case ST_TEMPLATES:		step = ST_DONE; break;

		/* enroll setup: the next id = the template count (ids stay
		 * contiguous: templates are only ever deleted all at once) */
		case ST_ENROLL_LED:		step = ST_ENROLL_COUNT; break;
		case ST_ENROLL_COUNT:
			if(code || replyLen < 3){
				fp_result(LED_RED);
				break;
			}
			enrollId = (d[0] << 8) | d[1];
			if(enrollId >= FP_SEARCH_PAGES){
				log_printf("enroll: library full");
				fp_result(LED_RED);
				break;
			}
			enrollPress = 1;
			log_printf("enroll id %u: press %u/%u", enrollId, enrollPress, FP_ENROLL_PRESSES);
			step = ST_GET_IMAGE;
			break;

		/* capture (scan and enroll) */
		case ST_GET_IMAGE:
			if(code != 0x02){
				sawFinger = 1;
			}
			if(code == 0x00){
				step = ST_GEN_CHAR;
			}else if(job == JOB_SCAN && clock_time_exceed(jobTick, FP_SCAN_WAIT_MS * 1000)){
				log_printf(sawFinger ? "scan: bad captures" : "scan: no finger");
				if(sawFinger){
					fp_result(LED_RED);
				}else{
					step = ST_DONE;		/* a touch without a finger seen: no colour */
				}
			}else if(job == JOB_ENROLL && clock_time_exceed(jobTick, FP_ENROLL_MS * 1000)){
				log_printf("enroll: timeout");
				fp_result(LED_RED);
			}else{
				fp_delay(50);	/* no finger yet / bad capture: try again */
			}
			break;
		case ST_GEN_CHAR:
			if(job == JOB_SCAN){
				if(code){
					fp_result(LED_RED);
				}else{
					step = ST_SEARCH;
				}
			}else if(code){
				step = ST_GET_IMAGE;	/* enroll: bad image, capture again */
			}else if(enrollPress < FP_ENROLL_PRESSES){
				enrollPress++;
				log_printf("enroll: lift finger, then press %u/%u", enrollPress, FP_ENROLL_PRESSES);
				step = ST_WAIT_LIFT;
			}else{
				step = ST_REG_MODEL;
			}
			break;
		case ST_SEARCH:
			fp_result(code ? LED_RED : LED_GREEN);
			break;

		/* enroll */
		case ST_WAIT_LIFT:
			if(code == 0x02){
				step = ST_GET_IMAGE;
			}else if(clock_time_exceed(jobTick, FP_ENROLL_MS * 1000)){
				log_printf("enroll: timeout");
				fp_result(LED_RED);
			}else{
				fp_delay(50);
			}
			break;
		case ST_REG_MODEL:
			if(code){
				fp_result(LED_RED);	/* the captures don't match each other */
			}else{
				step = ST_STORE;
			}
			break;
		case ST_STORE:
			if(!code){
				log_printf("enrolled id %u", enrollId);
			}
			fp_result(code ? LED_RED : LED_GREEN);
			break;

		/* clear */
		case ST_EMPTY:
			if(!code){
				log_printf("all templates deleted");
			}
			fp_result(code ? LED_RED : (LED_RED | LED_GREEN));	/* yellow = cleared */
			break;

		case ST_SHOW:
			fp_delay(FP_RESULT_MS);
			step = ST_DONE;
			break;

		case ST_RAW:
		default:
			step = ST_DONE;
			break;
	}
}

/**********************************************************************
 * Public
 */
void fp_init(void)
{
	fp_power(0);
	state = FP_OFF;
	touchArmed = !gpio_read(GPIO_FP_TOUCH);
	bls_pm_setWakeupSource(PM_WAKEUP_PAD);
	cpu_set_gpio_wakeup(GPIO_FP_TOUCH, Level_High, touchArmed);
	log_printf("fp init, touch %u", gpio_read(GPIO_FP_TOUCH) ? 1 : 0);
}

void fp_enroll(void)
{
	log_printf("enroll: put the finger on the sensor");
	fp_start(JOB_ENROLL);
}

void fp_clearAll(void)
{
	log_printf("clear all templates");
	fp_start(JOB_CLEAR);
}

bool fp_busy(void)
{
	return state != FP_OFF;
}

void fp_task(void)
{
	int r;

	/* touch: TOUCH_OUT rising edge while the module is off */
	u8 touch = gpio_read(GPIO_FP_TOUCH) ? 1 : 0;
	if(state == FP_OFF){
		if(!touch && !touchArmed){
			touchArmed = 1;
			cpu_set_gpio_wakeup(GPIO_FP_TOUCH, Level_High, 1);
		}else if(touch && touchArmed){
			touchArmed = 0;
			cpu_set_gpio_wakeup(GPIO_FP_TOUCH, Level_High, 0);
			touchCount++;
			log_printf("touch #%u", touchCount);
			led_blip();
#if FP_SCAN_ON_TOUCH
			fp_start(JOB_SCAN);
#endif
		}
	}

	if(rxStuck){
		log_printf("uart irq stuck, rx irq disabled");
		rxStuck = 0;
	}
	if(rxErr){
		log_printf("uart rx overflow x%u", rxErr);
		rxErr = 0;
	}

	switch(state){
		case FP_OFF:
			break;
		case FP_BOOT:
			r = fp_parse();
			if(r == 2){
				log_printf("module ready (0x55) after %u ms", (clock_time() - stateTick) / (1000 * sysTimerPerUs));
				state = FP_READY;
			}else if(clock_time_exceed(stateTick, FP_BOOT_MS * 1000)){
				log_printf("no 0x55 after %u ms, trying anyway", FP_BOOT_MS);
				state = FP_READY;
			}
			break;
		case FP_READY:
			fp_jobStep();
			break;
		case FP_WAIT:
			r = fp_parse();
			if(r == 1){
				fp_jobReply();
			}else if(clock_time_exceed(stateTick, FP_REPLY_MS * 1000)){
				log_printf("timeout waiting for reply to %02X (%u bytes received since power-on)", lastCmd, (u16)(rxHead - rxStart));
				if(lastCmd == CMD_CONTROL_BLN && ledOffPending){
					ledOffPending = 0;	/* not fatal: go on with the job */
					state = FP_READY;
				}else if(lastCmd == CMD_HANDSHAKE && ++retries < 3){
					state = FP_READY;	/* the module may still be booting */
				}else if((job == JOB_SCAN || job == JOB_ENROLL || job == JOB_CLEAR) && step != ST_SHOW){
					fp_result(LED_RED);
				}else{
					fp_finish();
				}
			}
			break;
		case FP_DELAY:
			fp_parse();
			if(clock_time_exceed(stateTick, delayMs * 1000)){
				state = FP_READY;
			}
			break;
		case FP_HOLD:
			r = fp_parse();
			if(r == 1){
				fp_logReply(reply[0], &reply[1]);	/* late / unsolicited reply */
			}
			if(!manualOn && clock_time_exceed(stateTick, FP_RAW_KEEP_MS * 1000)){
				fp_power(0);
				log_printf("fp off");
				state = FP_OFF;
			}
			break;
	}
}

/* line levels + UART loopback, to tell wiring from protocol problems */
static void fp_diag(void)
{
	u8 rx0 = 0, rx1 = 0, tch;

	if(state != FP_OFF){
		log_printf("diag: module must be off");
		return;
	}
	/* RX (module TX) with the 100k pull-down: high only if the module
	 * drives it. Try both PD4 levels so a wiring/polarity mistake shows. */
	gpio_set_input_en(GPIO_FP_RX, 1);
	gpio_set_input_en(GPIO_FP_PWR, 1);	/* read back the real pad level */
	gpio_setup_up_down_resistor(GPIO_FP_RX, PM_PIN_PULLDOWN_100K);
	for(int lvl = 1; lvl >= 0; lvl--){
		gpio_write(GPIO_FP_PWR, lvl);
		sleep_us(150 * 1000);
		rx1 = gpio_read(GPIO_FP_RX) ? 1 : 0;
		tch = gpio_read(GPIO_FP_TOUCH) ? 1 : 0;
		log_printf("diag: PD4=%u (pad reads %u): module TX line %u, touch %u",
				lvl, gpio_read(GPIO_FP_PWR) ? 1 : 0, rx1, tch);
		if(lvl == 1){
			rx0 = rx1;
		}
	}
	/* rx0 = TX line with PD4 high, rx1 = with PD4 low */
	if(rx0 == rx1){
		log_printf("diag: %s", rx1 ? "TX line high either way: VCC not switched by PD4?"
				: "TX line low either way: module unpowered (battery?) or TX wire open");
	}else{
		u8 onLvl = rx0 ? 1 : 0;
		log_printf("diag: module powered with PD4=%u: %s", onLvl,
				(onLvl == FP_PWR_ON) ? "matches board.h" : "flip FP_PWR_ON in board.h");
	}
	gpio_set_input_en(GPIO_FP_PWR, 0);
	gpio_write(GPIO_FP_PWR, FP_PWR_ON);

	/* internal loopback: checks our UART TX/RX path without the module */
	fp_uartOn();
	reg_uart_ctrl1 |= FLD_UART_CTRL1_LOOPBACK;
	uart_ndma_send_byte(0xA5);
	uart_ndma_send_byte(0x5A);
	uart_ndma_send_byte(0x33);
	sleep_us(2000);
	log_printf("diag: loopback received %u bytes (expect 3): %02X %02X %02X", (u16)(rxHead - rxStart),
			rxRing[rxStart % RX_RING], rxRing[(rxStart + 1) % RX_RING], rxRing[(rxStart + 2) % RX_RING]);
	reg_uart_ctrl1 &= ~FLD_UART_CTRL1_LOOPBACK;

	/* now listen to the module for a moment (0x55 after power-on?) */
	rxStart = rxTail = rxHead;
	fp_cmd0(CMD_HANDSHAKE);
	sleep_us(300 * 1000);
	u16 n = rxHead - rxStart;
	log_printf("diag: %u bytes from the module after handshake", n);
	if(n){
		u8 tmp[16];
		for(u16 i = 0; i < n && i < 16; i++){
			tmp[i] = rxRing[(rxStart + i) % RX_RING];
		}
		log_hex("diag: rx", tmp, n < 16 ? n : 16);
	}
	rxTail = rxHead;
	fp_power(0);
}

static int hexval(char c)
{
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/*
 * BLE text commands:
 *   info | scan | on | off | raw <hex payload, e.g. "raw 0f">
 *   dump / clear are handled by app_ble.c (log streaming)
 */
void fp_command(const char *s)
{
	log_printf("> %s", s);

	if(!strcmp(s, "info")){
		fp_start(JOB_INFO);
	}else if(!strcmp(s, "scan")){
		fp_start(JOB_SCAN);
	}else if(!strcmp(s, "enroll")){
		fp_enroll();
	}else if(!strcmp(s, "clearfp")){
		fp_clearAll();
	}else if(!strcmp(s, "diag")){
		fp_diag();
	}else if(!strcmp(s, "on")){
		manualOn = 1;
		if(state == FP_OFF){
			log_printf("fp on");
			fp_power(1);
			state = FP_HOLD;
			stateTick = clock_time();
		}
	}else if(!strcmp(s, "off")){
		manualOn = 0;
		if(state != FP_OFF){
			fp_power(0);
			log_printf("fp off");
			state = FP_OFF;
			job = JOB_NONE;
		}
	}else if(!memcmp(s, "raw ", 4)){
		const char *h = s + 4;
		rawLen = 0;
		while(h[0] && h[1] && rawLen < sizeof(rawCmd)){
			int a = hexval(h[0]), b = hexval(h[1]);
			if(a < 0 || b < 0){
				break;
			}
			rawCmd[rawLen++] = (a << 4) | b;
			h += 2;
			if(*h == ' '){
				h++;
			}
		}
		if(rawLen){
			fp_start(JOB_RAW);
		}else{
			log_printf("raw: bad hex");
		}
	}else{
		log_printf("commands: info scan enroll clearfp diag on off raw <hex> dump clear");
	}
}
