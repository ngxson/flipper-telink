## TLSR825x facts used

- Chip ID: read 3 bytes at `0x007d` → rev, ID lo, ID hi. ID `0x5562` means TLSR825x; both devices here are rev 0x02.
- CPU control `[0x0602]`: `0x05` = stop, `0x88` = run. Reading it back as `05` confirms the halt.
- SRAM is at SWS address `0x40000` to `0x4ffff`.
- Flash through the MCU's SPI master (no RX/TX needed): `[0x0d]` = CS/ctrl (bit0 CS, bit1 SDO, bit3 RD), `[0x0c]` = data. Read sequence: `[0x0d]=0` (CS low), write cmd `0x03` and 3 address bytes to `[0x0c]` one at a time, write the two bytes `00 0a` at `0x0c` (start auto-read), FIFO-read N bytes from `0x0c`, then `[0x0d]=1`. JEDEC ID uses `0x9f` and the same flow.
- Analog regs: `[0xb8..0xba] = addr, data, ctrl`. Read: write `{addr, 0, 0x40}` at 0xb8, read 0xb9..0xba (0xba bit0 = busy), then `[0xba]=0`. Write: `{addr, val, 0x60}`, then `[0xba]=0`. Analog reads return constant garbage (e.g. 0x46) when the ALGM block is in reset or unclocked (core `0x61` bit3 = reset, `0x64` bit3 = clock enable; see pvvx `EnableClkALGM`).
- GPIO digital regs per port (PA=0x580, PB=0x588, PC=0x590, PD=0x598, PE=0x5a0): `+0 in, +1 ie, +2 oen (1 = input), +3 out, +4 pol, +5 ds, +6 gpio(1 = GPIO, 0 = special function), +7 irq`. **PB and PC `ie` and `ds` live in analog regs** (PB ie 0xbd, PB ds 0xbf, PC ie 0xc0, PC ds 0xc2), so the digital bytes there read back as dummy 0xFF.
- Pull resistors: analog `0x0e + 2*port + (pin>=4)`, 2 bits per pin: 0 float, 1 1M pull-up, 2 100k pull-down, 3 10k pull-up. Wake polarity/enable *(likely, partly verified)*: analog 0x21–0x25 polarity (bit set = wake on low), 0x27–0x2b enable.
- PWM: `0x780` enable, `0x781` enable bits (bit0 = PWM0), `0x782` mode, `0x783` invert, `0x794 + 4*n` = cmp (16-bit) | max (16-bit) for channel n.
- Flash layout: app at 0 (`KNLT` magic at 0x08, app size at 0x18). The MAC/IEEE is at **0x76000** on 512 KB parts and **0xFF000** on 1 MB parts. Stored byte order is `b0..b5` = low 6 bytes little-endian, then `b6 b7` = the top 2 bytes swapped (e.g. `2a 66 16 38 c1 a4 06 f1` → IEEE `0xa4c13816662af106`).

## Activation and target behaviour (important)

- Both test devices keep running on their **own batteries**: switching the Flipper 3V3 off does not reset them. Verified by writing a RAM marker and seeing it survive 3 s of "power off".
- So `act` (which power-cycles, then spams CPU-stop) only works by luck: the stop has to land during one of the target's short wake-ups. Use long windows, e.g. `sws act c0 10000`, or press the device's button during the spam. `act all` cycles through the pins.
- Once halted, the CPU stays halted until `sws run` or a real reset. The device is then offline in Zigbee2MQTT and draining its battery, so resume it when done.
- After `sws run` the stock firmware goes back to deep sleep; `catch 30000` (stop-spam with no power cycle) caught no wake-up within 30 s on the ZG-226Z.
- A target halted early in boot shows reset-default GPIO registers and broken analog reads. To learn the real pin configuration, read the firmware's `gpio_init` instead (see below).
- The first flash JEDEC read right after activation once returned garbage (`17 A8 00`); the next 10 reads were all correct. Discard that first read and check JEDEC before any erase/write.

## Pin diagnostics (`pins`, `adc`, `decay`)

- `pins`: reads each pin with pull-down, pull-up and no pull. "LOW regardless" means it's shorted to GND or actively driven.
- `adc`: open-circuit voltage with every Flipper pull released (so nothing from the Flipper back-powers the target).
- `decay`: charge the pin, release it, and time the flip. A truly floating wire holds its level for over 200 ms; a pin on a pulled-up target rises in about 25–40 µs.
- A good SWS pin on these boards measured about 1.37–1.56 V open-circuit and rose low→high in about 26–37 µs.

## Devices

### ZG-204ZL (HOBEIAN PIR + light sensor, Z2M model "ZG-204Z")
- Flash GigaDevice `C8 60 14` (1 MB). Raw dump `dumps/private/zg204zl_full.bin` CRC32 `E5C63A4F`; sanitized `dumps/zg204zl_full.bin` CRC32 `F29A4D07`. Strings: `@@|ZG-204Z|`, `CNTOP ZG_ZIGBEE_SDK V1.2`.
- IEEE `0xa4c13816662af106` (at 0xFF000). In the raw dump, Zigbee network records at around 0x34214 hold that IEEE address followed by NWK addr `0xACA0`; this matches Zigbee2MQTT.
- Used flash: 0x00000–0x21000 (app), scattered sectors around 0x40000 and 0x76000, and 0xFE000–0xFFFFF (calibration/MAC; preserve it when flashing).
- Live register snapshot (inferred roles; the `watch` test saw no pin changes because the user didn't press or trigger anything during the window):
  - PA7 = SWS
  - PB1 = output high (UART TX)
  - PB7 = input, 10k pull-up (UART RX)
  - PA1 = input, 10k pull-up, wake on low (likely the **button**)
  - PD4 = the only port-D input, no pull (likely the **PIR output**)
  - PD7 = output high, PD3 = output low (LED and/or PIR enable)
  - PB6 = reads high (possibly the light sensor)
- Wiring as connected: SWS on Flipper C0. The wire on C1 was shorted to GND.

### ZG-226Z (HOBEIAN alarm/buzzer; firmware identifies as **ZG-228Z**)
- Flash GigaDevice `C8 60 13` (512 KB). Raw dump `dumps/private/zg226z_full.bin` CRC32 `43276E4E`; sanitized `dumps/zg226z_full.bin` CRC32 `92101438`. IEEE `0xa4c1382debfbf4a6` (at 0x76000).
- Pinout from static analysis of the dump:

  | Function | Pin | Evidence |
  |---|---|---|
  | Buzzer | **PC1** | `gpio_set_func(PC1, 20)` = PWM0_N, PWM0 enabled, init cmp/max `0x41a0/0x8340`; stop routine returns it to a GPIO output driven low (0xE374, 0xE41C, tone setup at 0xE330) |
  | LED | **PD4** | GPIO output, low at init; toggled by timer callbacks (0xF41C–0xF6A0) |
  | Button | **PD3** | input, 10k pull-up, active low; long-press counter up to 199 (0xD9B4) |
  | UART | PB1 TX / PB7 RX | PB1 output high with 1M pull-up; PB7 input with 10k pull-up |
  | SWS | PA7 | 1M pull-up |
  | **Vibration sensor** | **PA0** | input, 1M pull-up; every level change wakes the chip and bumps a pulse counter (0xDEE0 → 0xDDD8). The counter resets every 3 s; vibration is reported once it reaches roughly 50 − sensitivity pulses |
  | ? | PB6 | floating input, also a wake source, read on demand (0xDC3C). Purpose unknown; not exposed in Z2M, so it can be ignored |
  | VBAT | PC5 | SDK ADC battery-voltage helper (0xF0F0, 0xAB26) |

- No enable pins: the RF power-amplifier driver (0x2F0–0x860) uses pin variables at RAM 0x842414/0x842420/0x842428 that are never set and are gated off by flags.

## Zigbee NV storage (Telink Zigbee SDK) and sanitizing

- The NV area is split into modules. Each starts with magic `7a 7a`, a module id and a header. It is followed by an index of 8-byte entries `len(u16) item(u8) status(u8) offset(u32)` (status `0x50` = valid), then the item records. Newer copies of an item are appended, so several copies exist.
- Module locations (same in both dumps):

  | Module | Address | Contents |
  |---|---|---|
  | 0 | 0x34000 | ZB_INFO / NIB |
  | 1 | 0x36000 | address table |
  | 2 | 0x38000 | item 0x04 = SSIB |
  | 3 | 0x3A000 | ZCL |
  | 4 | 0x3C000 | NWK frame counters |
  | 6 | 0x7A000 | app settings |
  | 7 | 0x7C000 | APS key pairs |

- **Secrets:**
  - Network key: in the SSIB record (module 2), 0x12 bytes after the `04 7a` item tag; the trust center (coordinator) IEEE address is in the same record.
  - Per-device APS link key with the trust center: module 7 (e.g. record at 0x7C400 = TC IEEE address + 16-byte key).
  - The firmware contains the well-known `ZigBeeAlliance09` key, which is public.
- `tools/sanitize_dump.py in out` blanks 0x34000–0x3DFFF and 0x7C000–0x7DFFF (factory-new state), keeps the app, module 6, MAC and calibration, and refuses to write if a key is still found. Always run it before publishing a dump. The device's own MAC/IEEE stays in the image; it's an identifier, not a secret.

## Static analysis (TC32)

- TC32 is Thumb-like but uses **different opcode encodings**, so ARM objdump output is garbage. Use `python3 tools/tc32dis.py <bin> <start> <end>`. It resolves PC-relative literals (`; [addr]=value`) and `tjl` call targets. The opcode table comes from [trust1995/Ghidra_TELink_TC32](https://github.com/trust1995/Ghidra_TELink_TC32), which also has a Ghidra module if deeper work is needed.
- Code runs from flash at address 0. Registers appear in code as `0x80xxxx` literals (REG_BASE 0x800000); RAM is around `0x84xxxx`.
- To find pin usage: scan literal pools for `0x8005xx` (GPIO) and `0x8007xx` (PWM), find the SDK helpers, then resolve the `r0` and `r1` constants before each `tjl`. Pins are usually built as `tmov r0,#imm; tshftl r0,r0,#n`. Pin encoding is `(port << 8) | (1 << bit)`, e.g. PD4 = 0x310, PC1 = 0x202.
- SDK helper addresses in the ZG-226Z build (other builds differ): `gpio_set_func` 0x1FB94, `gpio_set_input_en` 0x1FAD8, `gpio_set_output_en` 0xA918, `gpio_write` 0xA948, `gpio_setup_up_down_resistor` 0x201D4, `analog_read` 0x12EC, `analog_write` 0x132C, `cpu_wakeup_init + gpio_init` (board defaults) 0xA978. The PWM1-on-PC3 routine at 0x1F8A0 is the SDK's 32 kHz crystal kick, not app logic.

# Task: custom firmware for the ZG-226Z (handover)

> **STATE (updated 2026-10-06, after the second agent's session): the firmware and the Flipper flasher are WRITTEN and BUILD CLEANLY, but nothing has been flashed or tested on real hardware yet.**
> See **"Status: firmware + flasher written"** near the bottom of this task for the full module map, protocol facts and gotchas, and **"Still to do"** for the remaining hardware steps (flash + validate + pair). Don't rewrite the app sources from scratch — extend what's in `custom_fw/zg226z/src/`.

**Working style the user asked for:** write fresh code. Do **not** do more reverse engineering of the stock firmware, and don't read large amounts of third-party source. Everything you need about the hardware and the Zigbee interface is below. Keep the user posted with short updates.

## Goal

New Zigbee firmware for the ZG-226Z vibration alarm in `custom_fw/zg226z`. It must work with the **stock Zigbee2MQTT definition for `ZG-228Z`** (vendor HOBEIAN) and add custom RTTTL melodies. **Nothing external may be added to Zigbee2MQTT**: no external converter and no custom exposes.

## Requirements

1. **Zigbee identity:** a battery end device. Basic cluster `manufacturerName = "HOBEIAN"`, `modelIdentifier = "ZG-228Z"`, power source battery. Talks Tuya datapoints over cluster **0xEF00**. Z2M's definition uses `tuya.modernExtend.tuyaBase({dp: true})`.
2. **Expose everything the original has.** These are the DPs from zigbee-herdsman-converters `src/devices/hobeian.ts`:

   | DP | Name | Tuya type | Values | Direction |
   |---|---|---|---|---|
   | 1 | vibration | enum (type 4; stock sends enum) | 1 = vibration, 0 = clear | report |
   | 4 | battery | value (type 2, 4-byte BE) | 0–100 % | report |
   | 6 | sensitivity | value | 1–50 (higher = more sensitive) | set + report |
   | 101 | vibration_siren | enum | 0 = OFF, 1 = ON (sound the alarm when vibration is detected) | set + report |
   | 102 | muffling | bool (type 1) | 1 = stop the alarm | set; report 0 when the alarm ends |
   | 103 | alarm_volume | enum | **repurposed:** 0 `low` = melody slot 1, 1 `middle` = slot 2, 2 `high` = slot 3, 3 `mute` = mute | set + report |
   | 104 | alarm_ring | enum | 0 mute, 1 beep, 2 music (music = play the melody slot selected by DP103) | set + report |
   | 105 | alarm | enum | 0 beep, 1 ring, 2 stop (trigger manually) | set; report 2 when stopped |
   | 106 | alarm_time | value | 0–1800 s alarm duration | set + report |

   Answer Tuya "data query" (cmd 0x03) by reporting every DP. Reply to "data request" (cmd 0x00) with "data response" (cmd 0x01) echoing the new value. Send unsolicited changes as "data report" (cmd 0x02). Payload format: `seq(u16) dp(u8) type(u8) len(u16 BE) value` — **correction (verified against zigbee-herdsman `buffaloZcl.ts`):** seq is a ZCL uint16, i.e. **little endian**; only the length field is big endian; there is no function byte. Persist the settings (DP 6, 101, 103, 104, 106) in NV.
3. **Volume:** no loudness control; the PWM duty is hard-coded to "low".
4. **Melodies:** 3 RTTTL slots stored in NV, each with a built-in default. Slot choice comes from DP103.
5. **BLE mode:** the TX pad (**PB1**) bridged to GND switches the device into BLE mode. While bridged:
   - the LED blinks fast
   - a GATT service lets you (1) send an RTTTL string to play right away as a test, and (2) write an RTTTL string into slot 1–3.
   - When the bridge is removed, go back to normal Zigbee-only behaviour; a reboot is fine.
6. **Button:** short press stops a sounding alarm, otherwise cycles DP104 and plays a short preview. A hold of about 3 s leaves the network and rejoins, so the device can pair again.

## Hardware (all known; see the ZG-226Z table in `../AGENTS.md`)

- **Buzzer:** PC1, driven as PWM0 output (`AS_PWM0_N`). It's a GPIO output held low when idle.
- **LED:** PD4, active high.
- **Button:** PD3, 10k pull-up, active low.
- **Vibration sensor:** PA0, 1M pull-up. Count level changes and use them as a wake source; the stock firmware compares the count against about 50 − sensitivity within 3 s.
- **BLE switch:** PB1, input with 1M pull-up; low means BLE mode.
- **VBAT:** PC5 ADC trick, the same as BZdevice's TH03Z board.
- **Flash:** 512 KB. Keep NV at 0x34000 / 0x7A000 and BLE NV at 0x74000 (the SDK defaults). **Never erase 0x76000 (MAC) or 0x77000 (frequency-offset calibration).** The app must stay below 0x34000.

## Build

- `custom_fw/zg226z/Dockerfile` builds image `tc32-build` (Debian amd64 with pvvx's Linux tc32 gcc). It's already built on this Mac.
- `custom_fw/zg226z/` already contains `SDK/` (Telink Zigbee SDK with BLE concurrent libs, Apache-2.0), `make/`, `makefile`, `src/patch_sdk/`, `src/includes/zb_config.h`, `src/boot.link`, `src/stack_cfg.h`, `LICENSE.BZdevice`, plus the new `src/version_cfg.h` and `src/board.h`.
- Build command (`make/src.mk` is already set up for the current sources — update it if you add/remove files):
  ```sh
  cd custom_fw/zg226z && docker run --rm --platform linux/amd64 -v "$PWD":/src tc32-build sh -c 'mkdir -p tools/linux && ln -sfn /opt/tc32/tc32 tools/linux/tc32 && make -s PROJECT_NAME=ZG228Z'
  ```
- The current build is 138 KB flash with `.bss+.data` ending at ~0x847EF0 (the boot.link asserts it stays below 0x848000) — keep new globals small.
- Toolchain check: upstream BZdevice's TH03Z target built fine this way, at 157 KB with Zigbee + BLE.
- Gitignore `custom_fw/*/build`, `custom_fw/*/bin`, `custom_fw/*/tools` and `custom_fw/*/tc32_gcc_v2.0.tar.bz2`.

## Status: firmware + flasher written (2026-10-06, session 2 of this task)

Items 1 and 2 below are **done** and build cleanly; items 3 and 4 still need the hardware. **The firmware has never run on the target yet** — expect first-boot debugging, not instant success. Known untested assumptions, roughly in blast radius order:

- PM wake sources (`cpu_set_gpio_wakeup` on PA0/PD3/PB1) and the deep-with-retention loop were written from BZdevice patterns, not measured on this board.
- The buzzer's `pwm_set_clk(CLOCK_SYS_CLOCK_HZ, 1 MHz)` assumes the PWM tick runs from the system clock; if the tones come out wrong, check `reg_pwm_clk` handling first.
- The alarm/vibration thresholds are straight ports of the stock behaviour (50 − sensitivity in 3 s) and may need tuning on real hardware.
- The BLE GATT chunked-write protocol and the 192-byte fixed slot reads are untested; `tools/ble_test.py` is the intended first test.
- `tuya_reportDp*` while not joined fails silently (by design) — don't mistake that for a bug.

If it doesn't boot: check `sws frd 0 32` against the expected `KNLT`-less image head after flashing (our image starts with the cstartup vectors, not the stock app magic).

**Firmware (`custom_fw/zg226z/bin/ZG228Z.bin`, 138 KB, built via the docker command above):**

| File | What |
|---|---|
| `src/app_cfg.h` | config + app NV item ids (0x5f ver, 0x60 cfg, 0x61..0x63 melodies); also defines `ev_poll_e` (the SDK os layer expects the app to provide it) and includes `stack_cfg.h` |
| `src/main.c` | dual-mode main loop; at boot reads PB1 (TX pad): low → BLE-only mode (adv on, LED fast blink, reboot when the bridge is removed), high → Zigbee-only. In BLE-only mode `ev_timer_process`/`ev_poll_process` are called directly because `zb_task` never runs |
| `src/device.c/h` | endpoint 1 (HA 0x0104, device id 0x0005): Basic ("HOBEIAN"/"ZG-228Z"), Power cfg, Identify, Tuya 0xEF00; stack/bdb init |
| `src/tuya_cluster.c/h` | 0xEF00 command handler: `dataRequest` (0x00) → per-DP callback + `dataResponse` (0x01) echo; `dataQuery` (0x03) → `dataReport` (0x02) with every DP. Reports go to the address of the last incoming command, else coordinator 0x0000/ep1 |
| `src/app.c/h` | DP state + NV persistence (DP 6/101/103/104/106), alarm state machine (vibration → siren for `alarm_time` s; beep vs melody from DP104, melody slot from DP103), vibration clear after 3 s, battery report every 6 h |
| `src/buzzer.c/h` | PC1 = PWM0_N, 1 MHz PWM tick, duty 1/10 ("low"); RTTTL parser/player (durations 1..32, octaves 4–7, #/b, dots, pauses), loop or note-limited preview; 3 melody slots in NV module 6, RAM copies reloaded after every retention wake (they are *not* in retention RAM, `RTTTL_MAX_LEN` = 192, RAM budget!) |
| `src/app_ble.c` + `ble_cfg.h` | BLE init (no security, MTU 247) + custom GATT service (base UUID ...FFE0): `FFE1 play` and `FFE2..FFE4 slot1..3` (read + write). Writes are **chunked**: append bytes, a `0x00` byte terminates and commits (play / store to NV), so it also works with MTU 23. Slot reads return the fixed 192-byte buffer (NUL padded). In normal Zigbee mode advertising stays **disabled** |
| `src/app_ui.c/h` | LED PD4 (blink via `TL_ZB_TIMER_SCHEDULE`), button PD3 (short = stop alarm / cycle DP104 + preview, ≥3 s hold = leave network + re-pair via `tl_bdbReset2FN` + `zb_resetDevice`), vibration PA0 pulse counting (≥ 50 − sensitivity pulses in 3 s → report DP1=1 and siren), wake sources (PA0/PD3/PB1 low) |
| `src/battery.c/h` | PC5/C5P ADC trick as BZdevice TH03Z, 2200–3000 mV → 0–100 %, DP4 + power-cfg attrs, reports only while joined |
| `src/app_pm.c` | ZB idle + BLE idle → deep-with-retention; keeps the CPU awake while the alarm/melody plays; never sleeps in BLE-only mode |
| `src/zb_appCb.c` | BDB callbacks (on join success: report all DPs), identify → LED blink, leave → reboot, captures the gateway address from basic-cluster reads |
| `src/zigbee_ble_switch.c/h` | RF slot switching, copied conceptually from BZdevice (sensors/LCD stripped); `bleOnly` blocks switching to the ZB slot |
| `src/patch_sdk/hw_drv.c` | trimmed: `sensors.h`/`ext_ota.h` includes and the `tuya_zigbee_ota()`/`battery_detect(1)` boot calls removed (battery is measured in `app_init` instead) |

Z2M protocol facts verified from zigbee-herdsman/converters sources: the `dp:true` payload is `seq(u16 ZCL-LE) + [dp(u8) type(u8) len(u16 **BE**) value]...` (no function byte), `dataQuery` has an empty payload, seq is written with ZCL uint16 (little endian). RAM ceiling: `.bss+.data` must end below 0x848000 (boot.link assert); the build ends at ~0x847EF0, so mind that when adding globals.

The RTTTL parser was host-unit-tested (extracted into a standalone C harness, ran on the Mac): defaults, durations 1..32, octave digits and `>`/`<`, sharps/flats, dots, pauses and rejection of malformed strings all behave. Two real bugs were caught this way: the defaults loop used to swallow the `:` that ends the defaults section (every melody failed to open!), and `>` octave shifts were unsupported while a built-in default melody used them.

**Flasher (`telink_sws`):** `sws.c` now has `sws_flash_read_status/busy/write_enable/wait_ready/erase_sector/write_page` (WREN 0x06, sector erase 0x20, page program 0x02 via `sws_write_fifo`, same SPI flow as `sws_flash_read`; sequence verified against pvvx/TLSRPGM `USB2SWire/src/main.c`). New CLI commands: `fstat`, `erase <addr> <len>`, and `flash <path> <addr> [verify]` (reads an SD file, refuses anything overlapping 0x76000–0x77FFF, erases the covered sectors, writes 256 B pages skipping all-0xff, reads back and verifies, prints CRC32; Ctrl-C aborts). Rebuild with `UFBT_HOME=$PWD/.ufbt ufbt` from `telink_sws/`.

**Helpers:** `tools/flash_custom.sh` (push bin → SD, act → jedec → flash 0 → erase NV 0x34000/0xA000 + 0x74000/0x2000 + 0x7A000/0x4000 → run) and `tools/ble_test.py` (bleak: scan, `play [rtttl]`, `slot <n> <rtttl>`, `read <n>`).

## Still to do

1. ~~Write the app sources~~ (done, see above)
2. ~~Flipper flasher erase/write~~ (done)
3. **Flash** (needs the user / hardware): first install the updated FAP on the Flipper (`cd telink_sws && UFBT_HOME=$PWD/../.ufbt ufbt` then `tools/relaunch.sh`, or just `tools/relaunch.sh` after a build) — the `sws flash` command only exists in the rebuilt app. Then halt the CPU (`sws act c0 10000`, pressing the button helps), check JEDEC `C8 60 13` (discard a first garbage read), `sws flash /ext/ZG228Z.bin 0`, erase the NV areas (0x34000–0x3DFFF, 0x74000–0x75FFF, 0x7A000–0x7DFFF) so the device starts factory-new, then `sws run` — `tools/flash_custom.sh` runs all of it. To roll back, flash `dumps/private/zg226z_full.bin` back with the MAC/calibration sectors skipped (the flasher refuses them anyway).
4. **Validate before handing over** (needs the hardware):
   - Fake the TX bridge over SWS by switching PB1's internal pull to 100k pull-down (analog reg 0x10, bits 2–3 = `10`), or just bridge the TX pad to GND.
   - Check the LED blink through GPIO registers; expect the fast blink in BLE mode.
   - `python3 tools/ble_test.py` from the Mac: play a test RTTTL string, write a slot and read it back (chunked writes, NUL terminator).
   - Zigbee pairing needs the user's Zigbee2MQTT, so ask them to enable permit-join; then verify all 9 DPs behave per the table above (settings persist across a battery pull).
