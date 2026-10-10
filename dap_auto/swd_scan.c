#include "swd_scan.h"

#include <furi.h>
#include <furi_hal.h>

/* Candidate pins, in the same order as the swd_probe app uses */
const GpioPin* const swd_scan_gpios[8] = {
    &gpio_ext_pc0,
    &gpio_ext_pc1,
    &gpio_ext_pc3,
    &gpio_ext_pb2,
    &gpio_ext_pb3,
    &gpio_ext_pa4,
    &gpio_ext_pa6,
    &gpio_ext_pa7,
};

static const char* const pin_names[8] = {"PC0", "PC1", "PC3", "PB2", "PB3", "PA4", "PA6", "PA7"};
static const uint8_t pin_header[8] = {16, 15, 7, 6, 5, 4, 3, 2};

const char* swd_scan_pin_name(uint8_t idx) {
    return idx < 8 ? pin_names[idx] : "?";
}

uint8_t swd_scan_pin_header(uint8_t idx) {
    return idx < 8 ? pin_header[idx] : 0;
}

/* Bisect masks: with these 6 patterns any single pin out of 8 can be isolated
 * by intersecting the masks that produced a response. */
static const uint8_t masks[6] =
    {0b10101010, 0b01010101, 0b11001100, 0b00110011, 0b11110000, 0b00001111};

#define PIN_COUNT 8

typedef struct {
    uint8_t io_swc; /* SWCLK candidates */
    uint8_t io_swd; /* SWDIO candidates */
    uint8_t cur_mask; /* which candidates act as clock this round */
    uint32_t delay_us; /* half-cycle delay */
} SwdScan;

static inline bool single_bit(uint8_t x) {
    return x != 0 && (x & (x - 1)) == 0;
}

static void scan_configure(SwdScan* s, bool output) {
    for(int io = 0; io < PIN_COUNT; io++) {
        uint8_t bit = 1u << io;

        /* neither a clock nor a data candidate: park it as plain input */
        if(!((s->io_swc | s->io_swd) & bit)) {
            furi_hal_gpio_init(swd_scan_gpios[io], GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
            continue;
        }

        if(s->cur_mask & bit) {
            /* clock */
            furi_hal_gpio_init(
                swd_scan_gpios[io], GpioModeOutputPushPull, GpioPullNo, GpioSpeedVeryHigh);
        } else {
            /* data */
            if(!output) {
                furi_hal_gpio_init(
                    swd_scan_gpios[io], GpioModeInput, GpioPullUp, GpioSpeedVeryHigh);
            } else {
                furi_hal_gpio_init(
                    swd_scan_gpios[io], GpioModeOutputOpenDrain, GpioPullUp, GpioSpeedVeryHigh);
            }
        }
    }
}

static void scan_set_clock(SwdScan* s, bool level) {
    for(int io = 0; io < PIN_COUNT; io++) {
        uint8_t bit = 1u << io;
        if((s->io_swc & bit) && (s->cur_mask & bit))
            furi_hal_gpio_write(swd_scan_gpios[io], level);
    }
}

static void scan_set_data(SwdScan* s, bool level) {
    for(int io = 0; io < PIN_COUNT; io++) {
        uint8_t bit = 1u << io;
        if((s->io_swd & bit) && !(s->cur_mask & bit))
            furi_hal_gpio_write(swd_scan_gpios[io], level);
    }
}

static uint8_t scan_get_data(SwdScan* s) {
    uint8_t bits = 0;
    for(int io = 0; io < PIN_COUNT; io++) {
        uint8_t bit = 1u << io;
        if((s->io_swd & bit) && !(s->cur_mask & bit))
            bits |= furi_hal_gpio_read(swd_scan_gpios[io]) ? bit : 0;
    }
    return bits;
}

static void scan_delay(SwdScan* s) {
    if(s->delay_us) furi_delay_us(s->delay_us);
}

static void scan_write_bit(SwdScan* s, bool level) {
    scan_set_clock(s, false);
    scan_set_data(s, level);
    scan_delay(s);
    scan_set_clock(s, true);
    scan_delay(s);
    scan_set_clock(s, false);
}

static uint8_t scan_read_bit(SwdScan* s) {
    scan_set_clock(s, true);
    scan_delay(s);
    scan_set_clock(s, false);
    uint8_t bits = scan_get_data(s);
    scan_delay(s);
    scan_set_clock(s, true);
    return bits;
}

/* LSB first */
static void scan_write_byte(SwdScan* s, uint8_t data, size_t bits) {
    for(size_t pos = 0; pos < bits; pos++) scan_write_bit(s, data & (1u << pos));
}

static void scan_write_bits(SwdScan* s, const uint8_t* data, size_t bits) {
    size_t byte_pos = 0;
    while(bits > 0) {
        size_t n = bits > 8 ? 8 : bits;
        scan_write_byte(s, data[byte_pos++], n);
        bits -= n;
    }
}

/* One detection round: switch SWJ-DP to SWD, line reset, DPIDR read request.
 * Evaluates the response on every data candidate pin. Returns true when a
 * valid DPIDR came back on some pin (io_swd/io_swc are narrowed then). */
static bool scan_detect_round(SwdScan* s, uint32_t* dpidr_out) {
    bool found = false;

    scan_set_data(s, false);
    scan_configure(s, true);

    /* JTAG to SWD switch sequence */
    for(int i = 0; i < 7; i++) scan_write_byte(s, 0xFF, 8);
    scan_write_byte(s, 0x9E, 8);
    scan_write_byte(s, 0xE7, 8);

    /* line reset */
    for(int i = 0; i < 7; i++) scan_write_byte(s, 0xFF, 8);
    scan_write_byte(s, 0x00, 8);

    /* DPIDR read request */
    uint8_t request = 0xA5;
    scan_write_bits(s, &request, 8);

    /* turnaround */
    scan_configure(s, false);

    uint8_t ack_bits[3];
    uint8_t rdata_bits[32];
    for(int pos = 0; pos < 3; pos++) ack_bits[pos] = scan_read_bit(s);
    for(int pos = 0; pos < 32; pos++) rdata_bits[pos] = scan_read_bit(s);
    uint8_t parity_bits = scan_read_bit(s);

    scan_set_data(s, false);
    scan_configure(s, true);

    for(int io = 0; io < PIN_COUNT; io++) {
        uint8_t bit = 1u << io;

        /* a pin driven as clock this round cannot carry data */
        if(s->cur_mask & bit) continue;

        uint8_t ack = 0;
        for(int pos = 0; pos < 3; pos++) {
            ack >>= 1;
            ack |= (ack_bits[pos] & bit) ? 0x04 : 0;
        }
        if(ack != 0x01) continue;

        uint32_t dpidr = 0;
        for(int pos = 0; pos < 32; pos++) {
            dpidr >>= 1;
            dpidr |= (rdata_bits[pos] & bit) ? 0x80000000u : 0;
        }
        if(dpidr == 0 || dpidr == 0xFFFFFFFF) continue;

        bool parity = (parity_bits & bit) != 0;
        if(__builtin_parity(dpidr) != parity) continue;

        /* got a valid response: this pin is SWDIO, and the clock is among
         * the pins driven as clock this round */
        *dpidr_out = dpidr;
        s->io_swd = bit;
        s->io_swc &= s->cur_mask;
        found = true;
    }

    return found;
}

static void scan_release_pins(void) {
    for(int io = 0; io < PIN_COUNT; io++)
        furi_hal_gpio_init(swd_scan_gpios[io], GpioModeAnalog, GpioPullNo, GpioSpeedLow);
}

bool swd_scan_auto(uint8_t* swdio_idx, uint8_t* swclk_idx, uint32_t* dpidr) {
    SwdScan s = {
        .io_swc = 0xFF,
        .io_swd = 0xFF,
        .cur_mask = 0,
        .delay_us = 1,
    };
    uint32_t id = 0;
    bool have_data = false;

    /* phase 1: bisect masks to find which pin is SWDIO. A target only
     * answers when its real SWCLK is driven as clock, so each round with a
     * response also narrows the clock candidates. */
    for(int round = 0; round < 18 && !have_data; round++) {
        uint8_t mask = masks[round % 6];
        s.cur_mask = mask;
        if(scan_detect_round(&s, &id)) have_data = true;
    }

    if(!have_data) {
        scan_release_pins();
        return false;
    }
    *dpidr = id;

    /* phase 2: the mask intersection does not always converge to a single
     * clock pin, so test each remaining candidate directly: drive only that
     * pin as clock and check for an ACK on the known data pin. */
    {
        uint8_t cand = s.io_swc;
        while(cand && !single_bit(s.io_swc)) {
            uint8_t bit = cand & (uint8_t)(-(int8_t)cand); /* lowest set bit */
            cand &= (uint8_t)~bit;

            SwdScan t = s;
            t.io_swc = bit; /* only this candidate as clock */
            t.cur_mask = bit;
            uint32_t id2 = 0;
            if(scan_detect_round(&t, &id2)) {
                /* ACK on the data pin: this pin is the clock. io_swc was
                 * already narrowed to the single bit inside the round. */
                if(id2 != 0) *dpidr = id2;
                s = t;
                break;
            }
            /* no response: drop this candidate from the set */
            s.io_swc &= (uint8_t)~bit;
        }
    }

    if(single_bit(s.io_swd) && single_bit(s.io_swc)) {
        scan_release_pins();
        *swdio_idx = (uint8_t)__builtin_ctz(s.io_swd);
        *swclk_idx = (uint8_t)__builtin_ctz(s.io_swc);
        return true;
    }

    scan_release_pins();
    return false;
}
