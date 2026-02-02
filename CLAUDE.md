# reTPFC — reThinkPad Fan Control

A from-scratch rewrite of TPFanCtrl2 in modern C++17, targeting Windows.
Controls ThinkPad fan speed by directly accessing the Embedded Controller (EC) via I/O ports.

## Build

- **Language**: C++17 (MSVC / Visual Studio 2022)
- **Build system**: CMake
- **Target platform**: Windows x86/x64
- **Port I/O driver**: PawnIO (signed kernel driver with sandboxed Pawn bytecode modules)
- **Requires**: Administrator privileges (for EC port access), PawnIO driver installed

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

## Project Structure

```
src/
  main.cpp                  Entry point, service/GUI dispatch
  config/
    config.h / config.cpp   INI config parsing, fan curve definitions
  ec/
    ec_access.h / .cpp      High-level EC read/write (temperatures, fan level)
    port_io.h / .cpp         Low-level I/O port protocol (EC command handshake)
  fan/
    fan_controller.h / .cpp  Fan control logic, mode state machine, safety checks
  ui/
    tray_icon.h / .cpp       System tray icon (color + text modes)
    settings_dialog.h / .cpp Main settings window
resources/
  icons/                     Tray icons (color-coded by temperature)
  app.rc                     Win32 resource file (dialogs, menus, icons)
```

## Architecture

### Layers (top to bottom)

1. **UI** — Win32 system tray icon + dialog window. Timer-driven display updates.
2. **Fan Controller** — State machine (BIOS/Smart/Manual modes). Reads temperatures, applies fan curve, writes fan level. Runs on a background thread.
3. **EC Access** — Reads temperature sensors, fan RPM, and writes fan control values by addressing EC registers.
4. **Port I/O** — Sends EC commands via I/O port handshake protocol. Uses PawnIO driver with the LpcACPIEC module for privileged port access (ports 0x62/0x66 only).

### Threading model

- Main thread: Win32 message loop (UI).
- Worker thread: Persistent thread that polls EC hardware every `Cycle` seconds. Posts results back to the UI thread via custom window messages (`WM_USER+N`).
- EC access is serialized with a mutex (`std::mutex`). The named mutex `Access_EC` synchronizes with BIOS/other drivers (per PawnIO LpcACPIEC module convention).

## Hardware Reference

### EC I/O Ports

The standard ACPI Embedded Controller interface:

| Control Port | Data Port | Description |
|-------------|-----------|-------------|
| 0x66 | 0x62 | Standard ACPI EC ports (used by all ThinkPads for fan control) |

Note: Some older ThinkPads also expose a proprietary LPC Channel 3 interface at ports
0x1600/0x1604, used by the Linux `tp_smapi` driver for battery threshold control. This is
a separate communication channel and is not needed for fan control. All ThinkPad fan control
(register 0x2F) uses the standard ACPI EC at 0x62/0x66.

### EC Protocol

**Status flags** (read from control port):
- `0x01` (OBF): Output buffer full — data ready to read
- `0x02` (IBF): Input buffer full — EC busy, wait before writing
- `0x08` (CMD): Input buffer has a command

**Commands** (written to control port):
- `0x80`: Read byte
- `0x81`: Write byte

**Read sequence**: Wait IBF clear → write `0x80` to ctrl → wait IBF clear → write offset to data → wait OBF set → read result from data.

**Write sequence**: Wait IBF clear → write `0x81` to ctrl → wait IBF clear → write offset to data → wait IBF clear → write value to data.

**Timeouts**: 1000ms max per flag wait, polled every 10ms.

### EC Registers

| Offset | Size | Purpose |
|--------|------|---------|
| 0x2F | 1 byte | Fan control. Bit 7 = BIOS mode (0x80). Bits 0-6 = manual speed level. |
| 0x31 | 1 byte | Fan selector. 0x00 = Fan 1, 0x01 = Fan 2. |
| 0x78–0x7F | 8 bytes | Temperature sensors (group 1) |
| 0x84–0x85 | 2 bytes | Fan RPM (little-endian 16-bit). RPM = (0x85 << 8) | 0x84. |
| 0xC0–0xC3 | 4 bytes | Temperature sensors (group 2, extended) |

### Temperature Sensors (12 total)

| Index | Offset | Default Name | Location |
|-------|--------|-------------|----------|
| 0 | 0x78 | cpu | Main processor |
| 1 | 0x79 | aps | Accelerometer / HDD protection |
| 2 | 0x7A | crd | PCMCIA slot area |
| 3 | 0x7B | gpu | Graphics processor |
| 4 | 0x7C | bat | Battery |
| 5 | 0x7D | x7d | Usually N/A |
| 6 | 0x7E | bat | Battery 2 |
| 7 | 0x7F | x7f | Usually N/A |
| 8 | 0xC0 | bus | Unknown |
| 9 | 0xC1 | pci | Mini-PCI / WLAN / Southbridge |
| 10 | 0xC2 | pwr | Power supply |
| 11 | 0xC3 | xc3 | Usually N/A |

Sensor values of 0x00 or 0x80 are invalid. Values > 127 are rejected.

### Fan Control Modes

**BIOS mode**: Write `0x80` to register 0x2F. EC firmware controls the fan. The application stops issuing commands.

**Smart mode**: Poll temperatures every `Cycle` seconds. Look up fan level from a temperature→level table (with hysteresis). Write level (0x00–0x7F) to register 0x2F.

**Manual mode**: Write user-specified level (0–7 standard) to register 0x2F. Auto-exit to Smart mode if max temperature exceeds `ManModeExit` (default 78C).

### Dual-Fan Protocol

1. Write `0x00` to 0x31 (select Fan 1)
2. Write fan level to 0x2F
3. Sleep 100ms
4. Write `0x01` to 0x31 (select Fan 2)
5. Write fan level to 0x2F
6. Sleep 100ms
7. Read back both fans to verify (retry up to 5 times on mismatch)

### Safety Requirements

- On program exit or suspension: always set fan to BIOS mode (`0x80`).
- On lid close (`GUID_LIDSWITCH_STATE_CHANGE`): switch to BIOS mode, restore on open.
- After `MaxReadErrors` consecutive EC read failures (default 10): revert to BIOS mode.
- Manual mode must auto-exit to Smart mode when any sensor exceeds `ManModeExit`.
- EC reads require two consecutive matching samples to accept (noise rejection).
- All EC access serialized with a mutex. Named mutex `Access_EC` for cross-process sync (per PawnIO LpcACPIEC convention).

## Config File Format (INI)

File: `TPFanControl.ini` in the executable directory. Comment lines start with `/`, `#`, or `;`.

### Key settings

```ini
Active=2              # 0=read-only, 1=disabled, 2=smart, 3=manual
Cycle=5               # Temperature poll interval (seconds)
IconCycle=1           # Tray icon update interval (seconds)
ManFanSpeed=0         # Manual mode default level
ManModeExit=78        # Auto-exit manual mode temperature (C)
MaxReadErrors=10      # Consecutive failures before BIOS fallback
StartMinimized=1      # Start in system tray
StayOnTop=1           # Window always on top
SlimDialog=0          # Compact UI layout
ShowTempIcon=1        # 1=text icon, 0=color icon
IconColorFan=1        # Green icon tint when fan active
Log2File=0            # Debug log
Log2csv=0             # CSV data log
```

### Fan curve format

```ini
Level=TEMP FAN HYST_UP HYST_DOWN
Level=50 0 0 0        # Fan off below 50C
Level=60 3 0 0        # Level 3 at 60C
Level=70 5 2 0        # Level 5 at 72C (hysteresis: +2 to activate)
Level=80 7 0 0        # Level 7 at 80C
Level=-1 0            # Terminator (required)
```

Dual profiles supported with `Level2=...` for Smart Mode 2.

### Sensor config

```ini
SensorName1=cpu       # Override default sensor names (3 chars)
SensorOffset1=5 -1 -1 # Subtract 5C from sensor 1 (all temps)
IgnoreSensors=pci,aps # Exclude from max-temp calculation
```

### Hotkeys

```ini
HK_BIOS=3 B          # Ctrl+Shift+B → BIOS mode
HK_Smart=3 S          # Ctrl+Shift+S → Smart mode
HK_Manual=3 M         # Ctrl+Shift+M → Manual mode
# Method: 1=Ctrl, 2=Shift, 3=Ctrl+Shift, 4=Alt, 5=Alt+Shift
```

## Development Phases

1. **Console config reader** — Parse INI, print fan curve. Learn: file I/O, structs, vectors, string parsing.
2. **EC access library** — Load PawnIO + LpcACPIEC module, read temps/fan RPM, print to console. Learn: PawnIOLib API, bitwise ops, hardware protocols.
3. **Fan controller (console)** — Poll + apply fan curve + write fan levels. Learn: std::thread, std::mutex, chrono, state machines.
4. **System tray icon** — Hidden window + tray icon with context menu. Learn: Win32 WNDCLASS, message loops, Shell_NotifyIcon.
5. **Settings dialog** — Temperature display, mode selection, status log. Learn: Win32 dialogs, timer-driven updates.
6. **Polish** — Windows service support, lid detection, hotkeys, installer.

## Conventions

- Modern C++17: use `std::string`, `std::vector`, `std::optional`, `std::mutex`, `std::thread`, structured bindings. No C-style strings or manual memory management except at the PawnIO boundary.
- No exceptions across module boundaries. Use return codes or `std::optional`/`std::expected` for error handling.
- All EC register constants defined in `ec/ec_access.h` as `constexpr`.
- Keep UI code strictly in `ui/`. Fan logic must not depend on UI headers.
- Prefer `uint8_t`/`uint16_t` for hardware register values.
- Named constants over magic numbers. Every EC offset and command has a named constant.
- All public functions, structs, enums, and struct members must have Doxygen docstrings using `@`-style tags (`@brief`, `@param`, `@return`). Static helper functions in `.cpp` files should also be documented. See `docs/code-style.md` for the full style guide.

## Dependencies

- **PawnIO driver**: User must install from https://pawnio.eu or `winget install namazso.PawnIO`
- **PawnIOLib**: Linked at build time (https://github.com/namazso/PawnIOLib)
- **LpcACPIEC.bin**: Signed PawnIO module for ACPI EC port access, distributed alongside the
  executable (signed builds from https://github.com/namazso/PawnIO.Modules/releases)

## Reference

The original TPFanCtrl2 source in this repo serves as a behavioral reference.
Key files for understanding the hardware protocol:
- `fancontrol/portio.cpp` — EC port I/O protocol
- `fancontrol/fanstuff.cpp` — EC register definitions, fan control, temperature reading
- `fancontrol/fancontrol.cpp` — UI, message loop, timer system
- `fancontrol/TVicPort.h` — Original kernel driver interface (replaced by PawnIO)

See also:
- `docs/pawnio-integration.md` — PawnIO architecture, API details, integration guide
- `docs/hardware-compatibility.md` — Supported models, known issues, EC firmware quirks
