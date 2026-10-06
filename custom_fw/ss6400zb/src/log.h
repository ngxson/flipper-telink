#ifndef _LOG_H_
#define _LOG_H_

/* RAM ring buffer of text log lines, streamed over BLE (see app_ble.c) */
#define LOG_BUF_SIZE	2048

void log_init(void);
void log_tick(void);		/* call from the main loop: keeps the ms uptime */
u32 log_uptimeMs(void);
void log_printf(const char *fmt, ...);	/* %s %c %d %u %x %X, width + '0' pad; adds "[s.mmm] " and "\n" */
void log_hex(const char *prefix, const u8 *data, int len);
void log_clear(void);

/* total bytes ever written; a reader keeps its own cursor in that space */
u32 log_head(void);
/* copy up to max bytes at *cursor, advance it; skips data already overwritten */
int log_read(u32 *cursor, u8 *dst, int max);
u32 log_oldest(void);

#endif
