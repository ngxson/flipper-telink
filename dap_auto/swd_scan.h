#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi_hal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-shot SWD pin auto-detect, ported from the swd_probe app (g3gg0).
 *
 * Scans all 8 GPIO header pins (PC0 PC1 PC3 PB2 PB3 PA4 PA6 PA7) with a
 * bisect mask scheme: each round some pins act as SWCLK and the rest as
 * SWDIO, and a DPIDR read request is evaluated per data pin. When a pin
 * answers with a valid ACK + DPIDR parity it is SWDIO, and the clock
 * candidates are narrowed to the pins that were clocking that round.
 * Typically converges in < 5 rounds (a few milliseconds).
 *
 * GND to the target is required; SWDIO/SWCLK may be wired to any two of
 * the 8 candidate pins, in any order. All other connected pins are ignored.
 *
 * On success stores the indices into swd_scan_gpios[] and the target DPIDR.
 */
bool swd_scan_auto(uint8_t* swdio_idx, uint8_t* swclk_idx, uint32_t* dpidr);

extern const GpioPin* const swd_scan_gpios[8];
const char* swd_scan_pin_name(uint8_t idx);
/* Flipper GPIO header pin number (1..18), 0 if N/A */
uint8_t swd_scan_pin_header(uint8_t idx);

#ifdef __cplusplus
}
#endif
