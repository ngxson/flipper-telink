# AGENTS.md

Notes for agents working on this repo: a Flipper Zero app that bit-bangs Telink's SWire (SWS) debug protocol, used to dump and (later) reflash TLSR825x Zigbee devices. Everything below was verified on real hardware unless marked *unverified*.

## Layout

| Path | What |
|---|---|
| `telink_sws/` | Flipper app (FAP). `sws.c/.h` = SWS master + TLSR825x helpers, `telink_sws.c` = app + `sws` CLI command |
| `tools/fcli.py` | Host helper: open the Flipper USB CLI and run commands (`python3 tools/fcli.py "sws id"`) |
| `tools/relaunch.sh` | Close the running app, rebuild, install, launch |
| `tools/ble_ota.py` | Push a firmware image over BLE (Telink OTA protocol, bleak): `scan`, `info`, `flash <bin>` |
| `tools/tc32dis.py` | TC32 (Telink CPU) disassembler for raw flash images |
| `dumps/` | Public, sanitized flash dumps (Zigbee network state and Tuya device credentials blanked by `tools/sanitize_dump.py`) |
| `dumps/private/` | Original raw dumps with network key / link keys. **Gitignored, never commit or publish** |
| `tools/sanitize_dump.py` | Blank the Zigbee NV modules (keys, PAN, addresses) and the Tuya credentials sector (auzKey etc., ZT3L 0xFB000) in a dump; refuses to write if any of them survives |
| `.ufbt/` | Project-local ufbt home with the SDK that matches the Flipper firmware (gitignored) |
| `custom_fw/` | Custom firmware, one subdirectory per device: `custom_fw/zg226z/` (ZG-226Z alarm, details in its `DETAILS.md`) and `custom_fw/ss6400zb/` (Tuya TS0044 remote: RE notes + BLE bring-up/OTA firmware, see its `README.md`; it builds against `custom_fw/zg226z/SDK`). Telink Zigbee+BLE SDK + build files taken from pvvx/BZdevice, plus a Docker toolchain |

## Build and run

- The Flipper runs **Unleashed `unlshd-089e`, API 87.8**. The default `~/.ufbt` SDK (official 1.2.0, API 79.2) builds FAPs that will not load. Always build with the project-local SDK:
  ```sh
  UFBT_HOME=$PWD/.ufbt ufbt            # build
  tools/relaunch.sh                     # close app + build + install + launch
  ```
  The SDK was installed with `UFBT_HOME=$PWD/.ufbt ufbt update --hw-target=f7 --url=https://unleashedflip.com/fw/unlshd-089/flipper-z-f7-sdk-unlshd-089.zip`. If the Flipper firmware is updated, check `info device` (`firmware.api.major/minor`) and re-run with the matching SDK URL. The `unlshd-093` SDK is API 88.9, which is too new.
- `ufbt launch` fails with "has to be closed manually" if the app is already open. Close it over the CLI by sending the full key sequence `input send back press`, then `short`, then `release`. `short` alone is ignored. `tools/relaunch.sh` does this.
- Serial port: `/dev/cu.usbmodemflip_Ngxson1`. Only one process can hold it, so stop CLI scripts before `ufbt launch` or `storage.py`.
- Copy files off the SD card with the ufbt toolchain Python, because the venv Python lacks `colorlog`:
  ```sh
  .ufbt/toolchain/current/bin/python3 .ufbt/current/scripts/storage.py -p /dev/cu.usbmodemflip_Ngxson1 receive /ext/x.bin dumps/x.bin
  ```

## The `sws` CLI command

While the app is open it registers `sws` on the Flipper CLI through `cli_registry_add_command_ex` (stack 4096). It unregisters the command on exit, taking a mutex so a running command finishes first. Run `sws help` for the list:

`pins` / `adc` / `decay` (pin diagnostics) · `pin <c0|c1|c3>` · `unit <ns>` (master unit, default 2000) · `div <n>` (slave TX divider, written to `[0xb2]`, default 0x40) · `power on|off` (Flipper 3V3 rail) · `act [pin|all] [ms] [delay_ms]` · `catch [ms]` · `stop` / `run` · `id` · `rd` / `wr` (core regs/RAM) · `ard` (analog regs) · `watch [secs] [all_ie]` (print GPIO input changes) · `dbg` (edge timings of last RX byte) · `jedec` · `frd` · `fstat` (flash SR1/SR2) · `fwsr <sr1> [sr2]` (write flash status, `fwsr 0` = unprotect) · `erase <addr> <len>` (4K sectors) · `flash <path> <addr> [verify]` (SD file → erase+write+verify). Erase/flash take the flash size from JEDEC, refuse the MAC/calibration sectors (0x76000–0x77FFF on 512 KB, last 8 KB on 1 MB) and refuse while the flash is write-protected · `dump <addr> <len> <path> [verify=1]`.

`dump` reads 256-byte chunks, re-reads each one and compares (up to 6 attempts), and prints progress every 4 KB plus a final CRC32 (the same CRC as Python `zlib.crc32`). It takes about 161 s for 512 KB and 212 s for 1 MB with verify on (a hand-held 1 MB dump ran at ~2.7 KB/s). Ctrl-C aborts. If a chunk still fails, it waits up to 10 min for contact (CPU stop + `[0xb2]` rewrite + 5 good ID reads) and resumes at the same chunk. There is no start-offset append, so to resume after an abort, dump the rest to a second file and concatenate on the host.

While the app is open, the GUI thread probes the target every ~50 ms when no CLI command holds the lock (live contact monitor). It shows CONTACT OK / PAD, NO REPLY / NO CONTACT / LINE LOW and drives the LED green / blue / red, with a high beep when contact is gained and a low beep when it is lost. During a CLI command it follows the command's own reads. OK toggles auto-halt (on by default: halts a chip the moment it answers).

Flipper pins: `c0` = PC0 (header pin 16, ADC1_IN1), `c1` = PC1 (pin 15, IN2), `c3` = PC3 (pin 7, IN4). 3V3 is header pin 9.

**Flash write protection:** the stock Tuya firmware on the TS0044 left SR1 = `0x1C` (BP0–BP2, whole chip protected). Erase and program are then silently ignored: the only symptom is WEL staying set and the data unchanged. Clear it with `sws fwsr 0`. That GD `C8 60 14` part ignores the two-byte WRSR (`fwsr 0 0`), so use the one-byte form. Check `fstat` before writing to any new device.

Relaunching the app turns auto-halt back on, so it halts a running target again. Follow with `sws run`.

## SWire protocol (as implemented)

- One unit = `u`. Bit 0 = 1u low + 4u high, bit 1 = 4u low + 1u high. A byte is `[cmd flag][d7..d0]` followed by 1u low (end). The cmd flag is 1 only for START (0x5a) and END (0xff).
- Write: `START 0x5a, A23..16, A15..8, A7..0, 0x00, data..., END 0xff`.
- Read: same header with RW byte `0x80`. For each byte the master drives 1u low and releases, and the slave answers 8 bits plus a 1u low end in **its own** unit timing (set by its `[0x00b2]`). The master then sends END.
- Slave RX decoding is ratio-based, so master units from 0.5 to 8 µs all work. Our RX decoder samples edges with DWT at 64 MHz and sets bit = 1 when low × 2 > period. If the first edge doesn't arrive within 400 µs, or any later edge takes more than 200 µs, the read counts as failed.
- Implementation: direct GPIO register access (MODER/BSRR/BRR/IDR). The pin is push-pull while the master transmits and an input with pull-up while the slave answers. Interrupts are disabled per byte only (`FURI_CRITICAL_ENTER`); the bus tolerates gaps between bytes.
- **Always write `[0x00b2]` before reading.** Firmware can set a very fast slave reply speed. The TS0044/ZT3L remote replied at ~1 µs/bit (0.17 µs lows), which our RX loop can't sample, so it looked like no contact until `sws div 0x40`. Writes always land because slave RX is ratio based. The probe, `catch` and the dump wait loop now do this.
- FIFO mode: `[0x00b3]=0x80` stops address auto-increment, which is needed to stream the SPI data register. Restore it with `[0x00b3]=0x00`.

## Task: custom firmware for the ZG-226Z

Moved to [`custom_fw/zg226z/DETAILS.md`](custom_fw/zg226z/DETAILS.md) (requirements, build, module map, status, remaining hardware steps). Read it only when working on `custom_fw/zg226z/`. Reverse-engineering notes for the ZT3L/SS6400zb remote (pin mappings, config format, button-latency analysis) are in [`custom_fw/ss6400zb/README.md`](custom_fw/ss6400zb/README.md).

## Other ideas

- Speed-up: lower the slave divider `[0xb2]` and the master unit; the current dump rate is about 3.2 KB/s with verify.
- Optional: load pvvx's floader into SRAM at 0x40000 and use the UART pads for faster transfers. This is not needed for dumping.

## References

- [pvvx/TLSRPGM](https://github.com/pvvx/TLSRPGM): SWire docs (`TelinkSWire/README.md`), USB2SWire master (`swire.c`, flash/analog sequences in `main.c`), `TlsrPgm.py`
- [pvvx/TlsrComProg825x](https://github.com/pvvx/TlsrComProg825x): UART-based SWS emulation and floader
- [pvvx/ZigbeeTLc](https://github.com/pvvx/ZigbeeTLc): custom firmware; issue #161 has a similar board (TLSR8253: SWS PA7, TX PB1, RX PB7)
- [trust1995/Ghidra_TELink_TC32](https://github.com/trust1995/Ghidra_TELink_TC32): TC32 opcode table / Ghidra module
