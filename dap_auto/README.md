# Flipper Zero as CMSIS DAP/DAP Link

## Local build and hardware status

This copy currently uses **fixed SWD pins**, despite the app's Auto name:
SWDIO = PC3 / header pin 7, SWCLK = PB3 / header pin 5, plus common GND.
The startup auto-scan is bypassed. SWD initialization parks unused JTAG pins
before configuring SWD, because PB3 is also the default JTAG TDO pin.

Build with the project-local Unleashed API 87.8 SDK, from the repository root:

```sh
(cd dap_auto && UFBT_HOME="$PWD/../.ufbt" ufbt)
python3 tools/dapctl.py exit
(cd dap_auto && UFBT_HOME="$PWD/../.ufbt" ufbt launch)
```

`dapctl.py exit` sends CMSIS-DAP vendor command **0x82**, then waits for the
normal Flipper CLI to return. The app stops through its normal GUI/thread cleanup
and restores USB without rebooting the Flipper. It works from any app scene.
`python3 tools/dapctl.py reboot` sends the existing **0x81** reboot command.
Both commands require PyUSB and pyserial; `--serial DAP_Ngxson` selects the probe.

On macOS, `ufbt launch` can report `Device not configured` after successful
installation because the app switches USB from `flip_Ngxson` to `DAP_Ngxson`.
Confirm that DAP USB has appeared, then probe with:

```sh
python3 dap_auto/raw_dap.py
openocd -f tools/silabs_status.cfg
```

Hardware checked on 2026-10-10:

- The GPIO ordering fix restored reliable DPIDR reads: `0x6ba02477`.
- AP0 ID: `0x84770001` (AHB); AP1 ID: `0x54770002` (Silicon Labs DCI).
- SE Get Status: `00000020 0101020c 010c0000 00000023 ffffffff`.
  This is a VSE response, xG22 family, SE firmware 1.2.12. The user confirmed
  the chip marking `MG22C22` (EFR32MG22C22x family). The complete ordering code
  and flash-size suffix are still unknown; DEVINFO cannot be read while locked.
- Read Lock Status: `0x23`. Debug lock is configured and active; secure debug
  is disabled; device erase is enabled. Reads of flash at `0x00000000` and CPU
  registers at `0xe000ed00` fail. **No firmware dump was obtained.**
- A follow-up attempt returned the same lock status. All 12 reads of flash,
  DEVINFO (`0x0fe08000`) and CPU ID failed at 100 and 20 kHz with both secure
  and non-secure AP transactions. DPIDR remained readable throughout; AP0 CSW
  was `0x02800020`. No firmware bytes were recovered.
- No flash erase, programming, or lock configuration commands were sent to the
  target. Erase-based unlock would destroy the firmware being sought.
- The rebuilt app's self-exit command restored the CLI, and relaunch was verified.

`tools/silabs_status.cfg` only issues Get Status and Read Lock Status through DCI.
The command formats and status bits are documented in Silicon Labs'
[DCI protocol](https://docs.silabs.com/btmesh/latest/efr32-dci-swd-programming/03-debug-challenge-interface-dci)
and [SE command list](https://docs.silabs.com/btmesh/latest/efr32-dci-swd-programming/05-se-command-list).
The [EFR32MG22 SDK header](https://github.com/SiliconLabs/gecko_sdk/blob/gsdk_4.4/platform/Device/SiliconLabs/EFR32MG22/Include/efr32mg22c224f512im40.h)
confirms the tested flash and DEVINFO base addresses. Its 512 KiB size applies
to the F512 part and must not be assumed from the partial `MG22C22` marking.
Silicon Labs documents the observed settings as
[standard debug lock](https://docs.silabs.com/bluetooth/10.1.0/series2-secure-debug/04-r-debuglock):
authenticated secure debug cannot be enabled after entering this state; the
documented transition back to unlocked erases main flash and RAM.

## Upstream documentation

Flipper Zero as a [Free-DAP](https://github.com/ataradov/free-dap) based SWD\JTAG debugger. Free-DAP is a free and open source firmware implementation of the [CMSIS-DAP](https://www.keil.com/pack/doc/CMSIS_Dev/DAP/html/index.html) debugger.

## Protocols

SWD, JTAG , CMSIS-DAP v1 (18 KiB/s), CMSIS-DAP v2 (46 KiB/s), VCP (USB-UART).

WinUSB for driverless installation for Windows 8 and above.

## Usage

### VSCode + Cortex-Debug

  Set `"device": "cmsis-dap"`
  
<details>
  <summary>BluePill configuration example</summary>
  
  ```json
{
    "name": "Attach (DAP)",
    "cwd": "${workspaceFolder}",
    "executable": "./build/firmware.elf",
    "request": "attach",
    "type": "cortex-debug",
    "servertype": "openocd",
    "device": "cmsis-dap",
    "configFiles": [
        "interface/cmsis-dap.cfg",
        "target/stm32f1x.cfg",
    ],
},
  ```
</details>

<details>
  <summary>Flipper Zero configuration example</summary>
  
  ```json
{
    "name": "Attach (DAP)",
    "cwd": "${workspaceFolder}",
    "executable": "./build/latest/firmware.elf",
    "request": "attach",
    "type": "cortex-debug",
    "servertype": "openocd",
    "device": "cmsis-dap",
    "svdFile": "./debug/STM32WB55_CM4.svd",
    "rtos": "FreeRTOS",
    "configFiles": [
        "interface/cmsis-dap.cfg",
        "./debug/stm32wbx.cfg",
    ],
    "postAttachCommands": [
        "source debug/flipperapps.py",
    ],
},
  ```
</details>

### OpenOCD
Use `interface/cmsis-dap.cfg`. You will need OpenOCD v0.11.0.

Additional commands: 
* `cmsis_dap_backend hid` for CMSIS-DAP v1 protocol.
* `cmsis_dap_backend usb_bulk` for CMSIS-DAP v2 protocol.
* `cmsis_dap_serial DAP_Oyevoxo` use DAP-Link running on Flipper named `Oyevoxo`.
* `cmsis-dap cmd 0x81` - reboot connected DAP-Link.
* `cmsis-dap cmd 0x82` - exit this local app and restore normal Flipper USB.

<details>
  <summary>Flash BluePill</summary>
  
  ```
openocd -f interface/cmsis-dap.cfg -f target/stm32f1x.cfg -c init -c "program build/firmware.bin reset exit 0x8000000"
  ```
</details>

<details>
  <summary>Flash Flipper Zero using DAP v2 protocol</summary>
  
  ```
openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_backend usb_bulk" -f debug/stm32wbx.cfg -c init -c "program build/latest/firmware.bin reset exit 0x8000000"
  ```
</details>

<details>
  <summary>Reboot connected DAP-Link on Flipper named Oyevoxo</summary>
  
  ```
openocd -f interface/cmsis-dap.cfg -c "cmsis_dap_serial DAP_Oyevoxo" -c "transport select swd" -c "adapter speed 100" -c init -c "cmsis-dap cmd 0x81" -c "exit"
  ```
</details>

### PlatformIO
Use `debug_tool = cmsis-dap` and `upload_protocol = cmsis-dap`. [Documentation](https://docs.platformio.org/en/latest/plus/debug-tools/cmsis-dap.html#debugging-tool-cmsis-dap). Remember that Windows 8 and above do not require drivers.

<details>
  <summary>BluePill platformio.ini example</summary>
  
  ```
[env:bluepill_f103c8]
platform = ststm32
board = bluepill_f103c8
debug_tool = cmsis-dap
upload_protocol = cmsis-dap
  ```
</details>
