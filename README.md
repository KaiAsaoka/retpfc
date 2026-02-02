# reTPFC

**reThinkPad Fan Control** — a from-scratch rewrite of [TPFanCtrl2](https://github.com/Shuzhengz/TPFanCtrl2) in modern C++17 for Windows.
Controls ThinkPad fan speed by directly accessing the Embedded Controller (EC) via I/O ports.

## Status

**Phase 2 complete** — INI config parser, EC access via PawnIO, temperature/RPM reading, fan control, safety handlers. Verified on ThinkPad T480 (dual-fan, dGPU).

Phase 3 (fan controller with polling loop and fan curve) is next.

## Requirements

- Windows 10/11
- Administrator privileges
- [PawnIO driver](https://pawnio.eu) installed (`winget install namazso.PawnIO`)
- C++17 compiler (MSYS2 UCRT64 GCC 13+ or Visual Studio 2022)
- CMake 3.20+

## Build

```bash
# MinGW (MSYS2 UCRT64)
cmake -B build -G "MinGW Makefiles"
cmake --build build

# Visual Studio 2022
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

# Mock EC build (no hardware required)
cmake -B build -G "MinGW Makefiles" -DRETPFC_MOCK_EC=ON
cmake --build build
```

The mock EC build simulates sensor data and EC protocol responses, allowing development and testing without a ThinkPad or PawnIO driver.

## Usage

```
retpfc.exe [path-to-config]
```

If no path is given, it looks for `TPFanControl.ini` in the working directory.

## Configuration

All settings are in `TPFanControl.ini`. See the comments in that file for details on each property. Key settings:

- **Active** — Operating mode (read-only / disabled / smart / manual)
- **Cycle** — Temperature polling interval in seconds
- **Level=** lines — Fan curve mapping temperatures to fan speeds
- **ManModeExit** — Safety temp that forces manual mode back to smart mode

## How It Works

reTPFC reads temperatures from the ThinkPad's Embedded Controller and sets the fan speed according to a configurable fan curve. It communicates with the EC through the standard ACPI interface (I/O ports 0x62/0x66) using the [PawnIO](https://github.com/namazso/PawnIO) kernel driver and its [LpcACPIEC](https://github.com/namazso/PawnIO.Modules) module.

### Fan Modes

- **BIOS** — EC firmware controls the fan. Safest mode.
- **Smart** — Automatic control using the fan curve in the config file.
- **Manual** — Fixed fan level. Auto-exits to Smart if temperature exceeds the safety threshold.

### Safety

- On exit, crash, or lid close: fan reverts to BIOS mode.
- After repeated EC read failures: fan reverts to BIOS mode.
- Manual mode auto-exits if any sensor exceeds the configured limit.

## Compatibility

Tested and verified on **ThinkPad T480** (dual-fan, discrete NVIDIA MX150). See [docs/hardware-compatibility.md](docs/hardware-compatibility.md) for the full list of supported and incompatible models, including T480-specific sensor behavior.

## Documentation

- [PawnIO Integration](docs/pawnio-integration.md) — Driver architecture and API details
- [Hardware Compatibility](docs/hardware-compatibility.md) — Supported models and known issues
- [Code Style](docs/code-style.md) — Doxygen documentation conventions

## License

See original [TPFanCtrl2](https://github.com/Shuzhengz/TPFanCtrl2) for license terms.
