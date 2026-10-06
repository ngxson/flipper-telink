# ZT3L / SS6400zb — Tuya TS0044 4-button scene switch (TLSR825x, 1 MB flash)

Reverse-engineered from the raw dump `dumps/private/zt3l_ss6400zb.bin` (CRC32 `F754CE4B`).
The public `dumps/zt3l_ss6400zb.bin` (CRC32 `39EFA430`) has the Zigbee NV modules
(0xD8000–0xEFFFF) and the Tuya credentials sector blanked. 0xFB000 held the product id
`ee8nrt2l` (kept), a 64-hex-digit key, the 32-char `auzKey` and a MAC copy.
Firmware is **Tuya SDK** (strings `TS0044`, `tuya_zigbee`), not a Telink-SDK app, so GPIO
roles come from a factory JSON config in flash rather than compiled-in constants.

## GPIO mappings

| Function | Pin | Notes |
|---|---|---|
| Button 1 | **PA0** | input, active low (`bt1_pin:0`, `bt1_lv:0`) |
| Button 2 | **PB5** | input, active low (`bt2_pin:5`) |
| Button 3 | **PC2** | input, active low (`bt3_pin:10`) |
| Button 4 | **PC3** | input, active low (`bt4_pin:11`) |
| Network LED | **PD7** | output, active high (`net_led_pin:16`, `net_led_lv:1`) |
| SWS | PA7 | fixed by silicon (Tuya pin 2) |

No pairing button, no per-button LEDs, no battery ADC pin (`samp_type:0`); battery % is
computed from the 2500–3000 mV range in the config.

## How the mappings were derived (code anchors)

- **Factory JSON config at flash 0xF8000**: magic `0xdeadbeef`, u32 CRC32 of the text at
  +4, u16 text length at +8, text at +0xA (verified: CRC `3A3F06A3` matches). Loader
  `0x18994`, parser `0x1874C` against a 39-entry schema table (flash 0x3D374 → RAM
  0x842334; entries `{key_ptr, dest, maxlen|type, flag}`), post-processor `0xB254`.
- Config struct at RAM **0x8449B4**, 12-byte records: net_bt@+16, bt1..bt6@+28/40/52/64/76/88,
  net_led@+100, led1..led6@+112…, each `{enable@+0, port@+4, bit@+5, mode@+6, lv@+7}`.
  The schema `btN_pin` dest points at the record's **bit** field, so the parsed value is
  overwritten in place by (port, bit).
- **Tuya pin-number → GPIO table at flash 0x3C8EC** (const `{key, value}` pairs, value
  bytes = `{port, bit}`; mapper fn `0x18A08` returns a pointer, callers read `[ptr]`=port,
  `[ptr+1]`=bit): 0=PA0, 1=PA1, 2=PA7, 3=PB1, 4=PB4, 5=PB5, 6=PB6, 7=PB7, 8=PC0,
  9=PC1, 10=PC2, 11=PC3, 12=PC4, 13=PD2, 14=PD3, 15=PD4, 16=PD7.
- Runtime button table at RAM 0x844540 (16 × 20-byte entries: `{cb@+0, port@+8, bit@+9,
  mode@+10, lv@+11, trig@+12, idx=port*16+bit@+17, flag@+18}`), built by `0xB8A0` →
  `0x14D98`; scan/press handler `0x147B0`/`0x14C14` reads GPIO `0x800580 + 8*port` with
  mask `1<<bit`. `gpio_set_func` is at 0x1FA60, pin encoding `(port<<8)|(1<<bit)`.

## Why a button press takes ~500 ms to reach Z2M

Measured symptom: press a button, and Zigbee2MQTT logs the action roughly 500 ms later.
The dominant cause is **intentional**: the firmware supports single / double / hold
actions per button (as exposed by Z2M's TS0044 definition), and a *single* click cannot
be reported until the double-click window has expired.

Verified event chain (addresses from the dump):

1. **Press/release detection** — the SDK debounce-scans the button table and calls the app
   callback `0x14948`, which posts the event into a software event queue with a
   **10 ms delay** (`0xE13C(btn, 10)`; the queue's tick is millisecond-based, see the
   tick manager at `0xDFA0`). Deferred dispatch: `0xE0A8` stores `{handler, deadline =
   tick + delay}` and the timer callback `0xE169` fires the handler at the deadline.
2. **Press** (`0xC2B4` → `0xC23C`): stores the press tick in the per-button state
   (`[0x84528c] + btn*8 + 4`) and **cancels any pending classification event**
   (`0xDEF4`) — a second press inside the window kills the not-yet-sent "single".
3. **Release** → classifier `0xC09C` computes hold duration = now − press tick:
   - **> 2999 ms** (0xBB7) → hold action (count 3), dispatched **immediately** (delay 0).
   - short click → count 1 (single) or 2 (double, if a previous click is still pending),
     and the action event is posted with a **300 ms delay** (`tmov r1,#150; tshftl
     r1,r1,#1` = 300 at 0xC0C2/0xC112/0xC11E).
4. **Dispatch** — 300 ms later (if no second press cancelled it), handler `0xC000` reads
   the click count and calls `0xBF38(action, count)` → `0xBE48(action, 0|1|2)` which
   builds the Tuya scene-switch Zigbee command (scene group/id from the table at
   0x8440A8) and sends it.

So a single click is reported **≥ 300 ms after release** by design — that is the
double-click discrimination window. The rest of the observed ~500 ms is stacked
overhead:

- two 10 ms event-queue hops (press and release are each deferred),
- the SDK debounce scan period,
- **wake latency**: the device dozes in deep sleep and stays awake only ~16 s after the
  last activity (the 15999/16000 ms window in the tick manager `0xDFA0`); a press that
  wakes it from sleep pays boot + radio re-init before anything is sent,
- Zigbee airtime and Z2M's own processing.

Double clicks and holds don't feel this slow from the firmware's side (a double is
detected on the second release, a hold fires immediately at release) — but the *single*
click, the most common action, always waits out the 300 ms window. A custom firmware
that doesn't need double-click actions (or reports them as a follow-up event) can send
on press and feel instant.

## Custom firmware (BLE + OTA + fingerprint demo)

`custom_fw/ss6400zb/` holds a fresh firmware for this remote. It is **BLE only for now**
(no Zigbee): it accepts new images over BLE OTA, streams a debug log over BLE, and runs a
fingerprint demo with an HLK-ZW101 module wired to the ZT3L pads (see below).

Progress (all verified on the real device, 2026-10-06):

| Version | What |
|---|---|
| v00.01 | LED blink + BLE OTA. Flashed once over SWS; OTA A → B → A tested with and without the Flipper attached |
| v00.03–v00.06 | ZW101 UART driver, BLE log + commands, `diag`; found the weak cell and the active-high power switch |
| v00.07 | module LED off at power-up; colours verified |
| **v00.08** (running) | demo: touch = identify (green/red 1 s), button 1 = enroll (blue blink), button 2 = delete all (yellow) |

Every image since v00.01 went in over BLE OTA. The SWS wire is no longer needed.

| What | Value |
|---|---|
| BLE name | `SS6400_XXXX` (last 2 MAC bytes; this unit: `SS6400_5BB7`) |
| Address | public, low 6 bytes of the IEEE at 0xFF000 (`A4:C1:38:80:5B:B7`) |
| LED (PD7) | 40 ms flash every 2 s advertising, every 0.5 s connected, solid during OTA, one flash per touch/button |
| Services | GAP, GATT, Device Information (FW revision `vRR.BB @0xSLOT`), Telink OTA, debug (0xFFF0) |
| Power | BLE suspend between adv (500 ms) / connection events; wake on LED edges, TOUCH_OUT high, button low. No suspend while the ZW101 is powered |
| Size | 49 KB flash, about 22 KB SRAM (v00.08) |

### Build

Uses the Telink Zigbee+BLE SDK in `../zg226z/SDK` (shared, not copied) and the
`tc32-build` docker image from `../zg226z/Dockerfile`:

```sh
custom_fw/ss6400zb/build.sh          # -> custom_fw/ss6400zb/bin/SS6400ZB.bin
```

`make/tl_check_fw.py` pads the image, sets the `5d 02` OTA marker at +6 and the size at
+0x18, and appends the CRC32 (so `zlib.crc32` of the whole file is `0xFFFFFFFF`).
Bump `APP_BUILD`/`APP_RELEASE` in `src/version_cfg.h` for every image you push, so the
FW revision shows which one runs.

`build.sh` always does a clean build because the makefile doesn't track header dependencies:
a version bump in `version_cfg.h` would otherwise not reach `main.o`.

Sources: `src/main.c` (init, LED, main loop, suspend mask), `src/app_ble.c` (GATT table,
adv, debug service: log streaming + command queue), `src/ble_ota.c` (slot selection),
`src/fp.c` (ZW101 driver and jobs), `src/buttons.c` (4 buttons), `src/log.c` (RAM log),
`src/irq.c` (BLE-only IRQ + fingerprint UART RX: the SDK's handler is the Zigbee/BLE
concurrent one), `src/board.h` (pins). No Zigbee stack is linked yet;
`drv_platform_init()` still sets the radio up for Zigbee, then `ble_radio_init()`
switches it to BLE.

### OTA (two slots)

- Slot A = 0x00000, slot B = 0x40000, 256 KB max each. The boot ROM starts the slot
  whose byte +8 is `0x4B` (`KNLT`), checking 0x00000 first.
- The transfer is the Telink OTA protocol of the BLE library (`otaWrite`): service
  `00010203-0405-0607-0809-0a0b0c0d1912`, char `...2b12`, write-without-response.
  The library writes to `ota_program_offset`, checks the CRC32, sets the new slot's
  flag, zeroes the old one and reboots. **Nothing in the SDK sets
  `ota_program_offset`**: `ble_ota.c` reads flash +8 at boot (flash reads are
  physical) and targets the other slot, then `bls_ota_clearNewFwDataArea()` erases
  leftovers there.
- Host side: `tools/ble_ota.py` (bleak, in a venv):
  ```sh
  python3 tools/ble_ota.py scan
  python3 tools/ble_ota.py info                     # e.g. "v00.01 @0x00000"
  python3 tools/ble_ota.py flash custom_fw/ss6400zb/bin/SS6400ZB.bin
  ```
  It runs at about 3 KB/s from macOS (37 KB in about 12 s) and reads the characteristic back every 16
  packets for flow control. pvvx's web `TelinkMiFlasher.html` should also work (same
  protocol), *untested*.
- A future Zigbee firmware must keep the OTA service (and this slot logic) or the
  device needs SWS again. Its image must stay under 256 KB.

### Flashing over SWS (first time, or to recover)

The stock Tuya firmware leaves the flash **write-protected** (SR1 = `0x1C`, BP0–BP2 =
whole chip). Erase/program then silently do nothing, and only WEL stays set. Clear it first
with the **one-byte** WRSR (this GD `C8 60 14` part ignores the two-byte form):

```
sws fstat                         # SR1 1C/1E = protected
sws fwsr 0                        # -> SR1 00
sws erase 0 0x40000               # stock Tuya bootloader 0x0-0x7000 + app 0x8000-0x3E000
sws flash /ext/SS6400ZB.bin 0 1
sws run
```

(The flasher now refuses erase/flash while BP bits are set.) Leave 0xD8000–0xFDFFF
(Tuya NV + factory config at 0xF8000) alone for now. A Zigbee build will use NV at 0xE0000
and BLE NV at 0xF7000, so erase those then. 0xFE000–0xFFFFF (calibration + MAC) is
refused by the flasher. Rollback: `dumps/private/zt3l_ss6400zb.bin` (CRC32 `F754CE4B`).

## HLK-ZW101 fingerprint module (firmware v00.08, verified on hardware)

### Wiring

ZT3L pads numbered from the top, L = left column, R = right column (SWS is the single
bottom pad):

| ZW101 | ZT3L pad | Firmware |
|---|---|---|
| 1 V_SENSOR | R8 3V3 (always on) | |
| 2 TOUCH_OUT | R5 D2 | input, 100k pull-down, high = finger; suspend wake source |
| 3 VCC | switch controlled by L6 D4 | **active high** (`FP_PWR_ON 1` in board.h, measured with `diag`) |
| 4 TX | L2 RX = PB7 | UART RX (57600 8N1, non-DMA, one irq per byte) |
| 5 RX | L1 TX = PB1 | UART TX |
| 6 GND | L8 GND | |

While the module is off, PB1/PB7 are floating inputs, so nothing back-powers it through its
IO diodes. VCC is on only while a job runs. With V_SENSOR always on, a touch still raises
TOUCH_OUT. Free pads left: L4 (B4), L7 (C0), R2 (C4).

### Demo behaviour (v00.08)

| Trigger | Action | Module LED |
|---|---|---|
| finger on the sensor (TOUCH_OUT rises) | GetImage (waits up to 2 s), GenChar(1), Search(1, 0..49) | green 1 s match, red 1 s no match or bad captures, nothing if no finger was ever seen |
| button 1 (PA0) | enroll at id = template count: 2 presses (GenChar buffer 1, lift, buffer 2), RegModel, Store | blinks blue while enrolling, then green 1 s / red 1 s (20 s timeout) |
| button 2 (PB5) | Empty (delete all templates) | yellow 1 s / red 1 s |
| buttons 3, 4 | logged only | |

Jobs are a step machine in `fp.c`: each step sends one command and the reply picks the
next step. Buttons and touches are ignored while a job runs (`busy`). The colour is set
with `PS_ControlBLN` mode 3, and the module powers off after 1 s, which also turns its
LED off.

### Results from the hand test (v00.08)

- 4 enrollments (ids 0–3) all stored. Each took 3.9–9.3 s, mostly waiting for the finger.
  The lift between presses is detected by polling GetImage until "no finger".
- 9 touches matched (scores 812–3142), 10 got "no match", 1 saw no finger. From touch to
  verdict takes about 0.5 s (capture 160 ms, extract 60 ms, search 25–250 ms). The module is
  powered for about 1.5 s per touch, including the 1 s colour.
- Clear-all was verified over BLE (`clearfp`), not with button 2.

### Module facts (measured)

- The "3V3" pad is the raw coin cell, with no regulator. With a used cell (2.47 V idle, 2.35 V at the
  module) the ZW101 does not boot: its TX line stays low and nothing answers. With a fresh cell it
  works. For real battery use, put a boost converter to 3.3 V with an enable pin on D4.
- After VCC on, the module sends `0x55` (ready) after 79 ms, every time. A few junk bytes
  (`00`/`F8`/`FC`/`FE`) come first while it powers up, and the parser skips them.
- ReadSysPara: library 50 templates, security level 3, packet 128, 57600 baud.
- It lights its LED at power-on, so the firmware sends `3C 04 00 00 00` (LED off) first.
  `PS_ControlBLN` 0x3C = mode (1 breathe, 2 flash, 3 on, 4 off), start colour, end colour,
  cycles. Colour bits: blue 1, green 2, red 4. Red, green, blue, white, off and blue flash
  (`3C 02 01 01 00`) are all accepted.
- Confirmation codes seen: 00 ok, 02 no finger, 09 no match.

### Open points

- **False rejects?** Several "no match" touches came between matches of the same id. If
  those were enrolled fingers, the 2-press enrollment is too weak. Try more presses
  (`FP_ENROLL_PRESSES`): check whether GenChar accepts buffer ids > 2 on this module, or use
  PS_AutoEnroll (0x31).
- Not yet confirmed visually: does the blue flash keep going through both enroll presses
  (cycles = 0)?
- Delete single templates (PS_DeletChar 0x0C). After that, ids are no longer contiguous,
  so the next free id must come from the index table (0x1F) instead of the template count.
- Power: no measurement yet. The module current for about 1.5 s per touch is the big item.
  Also check the idle current with V_SENSOR always on.
- Battery: a boost converter (see above). Report the voltage over BLE (the PC5 VBAT trick).
- Zigbee: send matches / rejects / enroll events to Z2M.
- `FP_SCAN_ON_TOUCH 0` turns a touch back into a plain counter (`touch #N`), useful when the
  module can't be powered.

### Debug over BLE

Service 0xFFF0: log notify 0xFFF1, text command write 0xFFF2. The log is a 2 KB RAM
ring with timestamps (`[s.ms]`). Subscribing replays it, then streams live lines. Use
`tools/ble_dbg.py` (bleak):

```sh
python3 tools/ble_dbg.py                         # buffered + live log
python3 tools/ble_dbg.py info                    # module info + template count
python3 tools/ble_dbg.py diag                    # line levels, power polarity, UART loopback
python3 tools/ble_dbg.py enroll -t 25            # same as button 1
python3 tools/ble_dbg.py on "raw 3c03020200" wait:3 off   # LED solid green for 3 s
```

Commands: `info`, `scan`, `enroll`, `clearfp`, `diag`, `on`/`off` (manual power, the module stays
on until `off`), `raw <hex payload>` (module stays on 10 s), `dump`, `clear` (log). `wait:<s>` is
host side only. Max 20 bytes per command.
