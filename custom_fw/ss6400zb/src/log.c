/**********************************************************************
 * Text log in a RAM ring buffer, timestamped with the uptime.
 **********************************************************************/
#include "tl_common.h"
#include <stdarg.h>
#include "log.h"

static u8 logBuf[LOG_BUF_SIZE];
static u32 logHead;		/* total bytes written */
static u32 logBase;		/* first byte still valid (after a clear) */

static u32 upMs;
static u32 upTick;

void log_init(void)
{
	logHead = logBase = 0;
	upMs = 0;
	upTick = clock_time();
}

void log_tick(void)
{
	u32 now = clock_time();
	u32 d = (now - upTick) / (1000 * sysTimerPerUs);
	if(d){
		upMs += d;
		upTick += d * 1000 * sysTimerPerUs;
	}
}

u32 log_uptimeMs(void)
{
	log_tick();
	return upMs;
}

static void log_putc(char c)
{
	logBuf[logHead % LOG_BUF_SIZE] = c;
	logHead++;
}

static void log_puts(const char *s)
{
	while(*s){
		log_putc(*s++);
	}
}

static void log_putnum(u32 v, u8 base, u8 upper, int width, char pad, u8 neg)
{
	char tmp[11];
	int n = 0;
	const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";

	do{
		tmp[n++] = dig[v % base];
		v /= base;
	}while(v && n < (int)sizeof(tmp));
	if(neg){
		if(pad == '0'){
			log_putc('-');
		}else{
			tmp[n++] = '-';
		}
		width--;
	}
	while(width-- > n){
		log_putc(pad);
	}
	while(n){
		log_putc(tmp[--n]);
	}
}

static void log_vprintf(const char *fmt, va_list ap)
{
	while(*fmt){
		char c = *fmt++;
		if(c != '%'){
			log_putc(c);
			continue;
		}
		char pad = ' ';
		int width = 0;
		if(*fmt == '0'){
			pad = '0';
			fmt++;
		}
		while(*fmt >= '0' && *fmt <= '9'){
			width = width * 10 + (*fmt++ - '0');
		}
		c = *fmt++;
		switch(c){
			case 's': log_puts(va_arg(ap, const char *)); break;
			case 'c': log_putc((char)va_arg(ap, int)); break;
			case 'd': {
				int v = va_arg(ap, int);
				log_putnum(v < 0 ? -v : v, 10, 0, width, pad, v < 0);
				break;
			}
			case 'u': log_putnum(va_arg(ap, u32), 10, 0, width, pad, 0); break;
			case 'x': log_putnum(va_arg(ap, u32), 16, 0, width, pad, 0); break;
			case 'X': log_putnum(va_arg(ap, u32), 16, 1, width, pad, 0); break;
			case 0: return;
			default: log_putc(c); break;
		}
	}
}

static void log_stamp(void)
{
	u32 ms = log_uptimeMs();
	log_putc('[');
	log_putnum(ms / 1000, 10, 0, 0, ' ', 0);
	log_putc('.');
	log_putnum(ms % 1000, 10, 0, 3, '0', 0);
	log_puts("] ");
}

void log_printf(const char *fmt, ...)
{
	va_list ap;
	log_stamp();
	va_start(ap, fmt);
	log_vprintf(fmt, ap);
	va_end(ap);
	log_putc('\n');
}

void log_hex(const char *prefix, const u8 *data, int len)
{
	log_stamp();
	log_puts(prefix);
	for(int i = 0; i < len; i++){
		log_putc(' ');
		log_putnum(data[i], 16, 1, 2, '0', 0);
	}
	log_putc('\n');
}

void log_clear(void)
{
	logBase = logHead;
}

u32 log_head(void)
{
	return logHead;
}

u32 log_oldest(void)
{
	u32 o = (logHead > LOG_BUF_SIZE) ? logHead - LOG_BUF_SIZE : 0;
	return (o > logBase) ? o : logBase;
}

int log_read(u32 *cursor, u8 *dst, int max)
{
	u32 o = log_oldest();
	if(*cursor < o){
		*cursor = o;
	}
	int n = 0;
	while(n < max && *cursor < logHead){
		dst[n++] = logBuf[*cursor % LOG_BUF_SIZE];
		(*cursor)++;
	}
	return n;
}
