# ZG-204ZL: PIR power-control investigation

Static analysis of `zg204zl_full.bin`, 2026-10-16. No hardware writes or current
measurements were performed.

- Size: 1,048,576 bytes.
- CRC32: `F29A4D07` (the public, sanitized dump).
- SHA256: `b08789cb6ed27864659f9d1db915d8930c8be2086279915a0494d6ed4e3d618f`.
- Firmware strings include `@@|ZG-204Z|` and `CNTOP ZG_ZIGBEE_SDK V1.2`.
- All code addresses below are **flash offsets**, as printed by `tools/tc32dis.py`.
  Names assigned to stock functions are inferred, not original symbols.

**Conclusion: no MCU-controlled PIR enable, shutdown, or power-gating pin was
identified.** PD4 is the PIR signal input, and PD7 is the active-low LED. PD3 is
initialized as an output low; a published teardown and custom firmware for the
ZG-204ZL identify it as part of the **light-sensor** circuit, not PIR power.
The evidence supports an always-powered PIR, but a binary cannot exclude an
unused hardware control connection or establish this unit's exact PCB wiring.

## Evidence in this dump

The SDK encodes a GPIO as `(port << 8) | bitmask`. For port D:
`PD3=0x308`, `PD4=0x310`, `PD7=0x380`. Register definitions in
`custom_fw/zg226z/SDK/platform/chip_8258/register.h` give:

| Register | CPU address | Meaning |
|---|---|---|
| Port-D input | `0x800598` | Sampled pin levels |
| Port-D input enable | `0x800599` | 1 enables input |
| Port-D output enable | `0x80059A` | **0 enables output** |
| Port-D output latch | `0x80059B` | Output value |

### Boot GPIO setup

At `0x8C02–0x8C06`, a 32-bit store of `0x80771000` to `0x800598`
sets the writable bytes to input-enable `0x10`, output-enable `0x77`, and
output latch `0x80`. Thus PD4 is an input, PD3 is an output low, and PD7
is an output high. This matches the earlier live snapshot in
`custom_fw/zg226z/DETAILS.md`.

This initialization is called from `0xEEB2`. It is not a PIR shutdown routine.

### PD4 is read, with its output driver disabled

Function `0xBDC8` performs the equivalent of:

```c
gpio_set_func(GPIO_PD4, AS_GPIO);        // 0xBDD2 -> 0x1D60C
gpio_set_input_en(GPIO_PD4, 1);          // 0xBDDA -> 0x1D550
REG8(0x80059a) |= 0x10;                 // disable PD4 output driver
gpio_setup_up_down_resistor(GPIO_PD4, 0); // 0xBDF0 -> 0x1DC4C: floating
return REG8(0x800598) & 0x10;
```

The motion state machine at `0xBE08` calls it at `0xBE12`, compares its value
with saved state, updates wake polarity at `0xBE62`, and processes motion and
reporting timers. Other PD4 reads occur in callbacks at `0xC068`, `0xC0EC`,
`0xC1C4`, `0xC1FC`, and `0xC238`.

### Sleep keeps PD4 as a wake source

The two five-byte wake entries originate at flash `0x1FEC8`, copied to RAM
`0x841FFC` by startup (`0x13C–0x14E`). Their pin values are PA1 (`0x002`) and
PD4 (`0x310`).

Function `0xBD98` sets the PA1 wake level to low and the PD4 wake level to the
opposite of PD4's current input. It passes both entries to `0xA8E0`, which calls
the GPIO wake configuration function at `0x1DD30` with enable=1.

The sleep path calls `0xBD98` at `0xAB04` and `0xAB1E`, immediately before its
sleep calls. These operations configure **MCU wake detection**, not sensor power.

### PD7 is the LED

The output writes in `0xD010–0xD294` change bit 7 of `0x80059B`:

- `0xD010`: drives PD7 high (LED off).
- `0xD0C4`: alternates high/low with configurable timers; `0xD194` supplies the
  blink durations.
- `0xD1E0`: drives PD7 low and schedules `0xD010` after the requested duration.
- `0xD248`: drives it low and schedules `0xD06C` after 1500 ms.
- The button handler calls the blink function with 150/150 ms at `0xBC7A`.

These are indicator patterns, not PIR power cycling. The external board mapping
below independently confirms the LED's active-low polarity.

### PD3 and other possible control paths

- PD3 starts low in the boot store above. No subsequent PIR-related PD3 switching
  was identified. The direct port-D output updates inspected mask bit 7, leaving
  bit 3 unchanged.
- The generic GPIO output functions at `0x8B40` and `0x8B70` are called by the
  radio code with pin values held in RAM `0x8423A4`, `0x8423B0`, and `0x8423B8`.
  Their examined callers are gated by flags at `0x8423B5/0x8423B6`; these are
  zero-initialized BSS. No PIR-specific pin assignment was identified there.
- The generic ADC GPIO routines at `0xB3C0/0xB434` can alter a supplied GPIO.
  The app's measurement routine at `0xCC74` supplies **PC5 (`0x220`)**, via
  `0xB668`, for battery measurement. This is not a PIR power-control path.
  The presence of PC4 in the generic ADC pin table at `0x1F49C` alone does not
  establish that this firmware samples PC4.
- The function labelled by the string `ZG_powerCb` at `0xCC50` only calls the
  logging function; it does not drive a sensor-enable pin.

## Independent hardware evidence and its limits

[pvvx's ZG-204ZL teardown](https://pvvx.github.io/ZG-204ZL-3.0/) maps PD4 to
the PIR signal, PD7 to the LED, and PC4/PD3 to the light-sensitive resistor
circuit. It identifies a 2.1 V XC6206 regulator. The
[XC6206 manufacturer datasheet](https://product.torexsemi.com/system/files/series/xc6206.pdf)
lists only VIN, VOUT, and VSS for the three-pin package: no enable pin.

The independent
[ZG-204ZL board definition](https://github.com/pvvx/ZigbeeTLc/blob/master/src/board_zg204zl.h)
defines PD3 as `GPIO_ILLUMI_ON`, PC4 as the illuminance ADC, PD4 as `GPIO_PIR`,
and PD7 as the LED. Its
[light-sensor driver](https://github.com/pvvx/ZigbeeTLc/blob/master/src/sensor_lx.c)
switches PD3 around ADC measurements. Its
[PIR driver](https://github.com/pvvx/ZigbeeTLc/blob/master/src/sensor_pir.c)
reads PD4 and adjusts wake polarity; it has no PIR power-switch operation.

These sources corroborate the interpretation but are **not proof that this
particular unit has the same PCB revision**. In particular, the light-sensor
power switching in that custom firmware must not be attributed to this stock
dump.

## Battery-saving implications

- Firmware can ignore motion and disable PD4 as a wake source while PIR operation
  is unwanted. That can avoid motion-triggered CPU/radio/LED activity, but does
  **not** remove the PIR circuit's own supply current.
- No existing GPIO was established that can cut the PIR's supply. Actual power
  removal would require confirming the PCB supply path and likely adding a
  switch/load switch. Any restart settling time would also need measurement.
- Do not drive PD4 low to try to turn the sensor off: it is connected to the
  sensor's output, not an identified enable input.
- No battery-life gain or PIR-only current can be quantified from this dump.

Useful next hardware check: with the battery removed, trace PIR VDD to the
regulator output and regulator VIN to the battery, looking for a series switch
or GPIO connection. If the rails are direct, firmware alone cannot power-gate it.

## Reproduce the key disassembly

```sh
python3 tools/tc32dis.py dumps/zg204zl_full.bin 0x8ba0 0x8c24
python3 tools/tc32dis.py dumps/zg204zl_full.bin 0xbd98 0xbe08
python3 tools/tc32dis.py dumps/zg204zl_full.bin 0xaab0 0xab34
python3 tools/tc32dis.py dumps/zg204zl_full.bin 0xd010 0xd2b4
python3 tools/tc32dis.py dumps/zg204zl_full.bin 0xcc50 0xccc4
```

Disassembly is mixed code and data: literal pools after returns are not executable
instructions. The investigation followed the relevant calls and register accesses;
it is not an exhaustive proof of all possible indirect execution paths.
