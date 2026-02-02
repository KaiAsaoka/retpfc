# Hardware Compatibility

## How ThinkPad Fan Control Works

All ThinkPad fan control operates through the ACPI Embedded Controller register HFSP at
offset 0x2F, accessed via I/O ports 0x62 (data) and 0x66 (command/status). The EC protocol
uses commands 0x80 (read) and 0x81 (write) to read and write this register.

Register 0x2F bit layout:

- Bit 7 set (0x80): Automatic/BIOS mode -- EC firmware controls the fan
- Bit 6 set (0x40): Disengaged mode -- open-loop, ramps to 100% duty cycle
- Bits 0-6, bits 7+6 clear: Manual mode -- fan level 0-7 (levels 8-63 behave as 7)

In manual mode the EC auto-regulates to maintain roughly constant RPM for the given level.
The mapping from level to RPM is model-dependent.

## Confirmed Working Models

The following models are confirmed to work with direct EC fan control via ports 0x62/0x66:

**TPFanCtrl2 confirmed list** (FanDjango V2.3.1+ fork):
- ThinkPad P53
- ThinkPad Z13
- ThinkPad Z16 Gen 1
- ThinkPad P16 Gen 1 AMD
- ThinkPad T16 Gen 1 AMD
- ThinkPad X1 Carbon Gen 12
- ThinkPad X230T
- ThinkPad P1 Gen 7

**Original TPFanControl compatible** (from tpfancontrol documentation):
- ThinkPad T-series (T400 through T490)
- ThinkPad X-series (X200 through X280)
- ThinkPad W-series
- ThinkPad P-series (with exceptions noted below)

**This project's target**: ThinkPad T480 -- uses standard ACPI EC at 0x62/0x66, confirmed
compatible with TPFanCtrl/TPFanCtrl2.

## Known Incompatible Models

### Models where EC reads fail entirely

| Model              | Problem                                | Notes |
|--------------------|----------------------------------------|-------|
| ThinkBook 13s Gen2 | Cannot read EC registers               | Not a ThinkPad -- uses a different EC layout entirely |
| ThinkPad P50       | Causes blackscreen on EC access        | Different fan control parameters; a special build (TPFanCtrl2 v2.1.5B) exists for manual mode only, but temperature reading is broken |
| ThinkPad L560      | Fan control does not work              | Marked "cantfix" -- firmware limitation that cannot be worked around in software |

### Models with BIOS override behavior

On newer ThinkPad models (roughly 2020+), the EC firmware aggressively reasserts its own
fan control algorithm after userspace writes to register 0x2F. This creates a tug-of-war:
the application sets a fan level, the BIOS resets it, the application sets it again, etc.

From TPFanCtrl2:

> "It will also sometimes take a longer time for the speed to update and the fans to spin up.
> This is caused by EC management for the newer ThinkPad BIOS. There is currently no way
> around it."

Symptoms on affected models:
- Fan speed changes take 5+ seconds to take effect
- Fans may desync (one ramps up, the other doesn't)
- EC periodically resets register 0x2F to its own value
- Workaround: switch to BIOS mode, then back to desired mode

This is a firmware-level limitation. No I/O driver (WinRing0, PawnIO, or custom) can fix it.
The EC firmware itself overrides the register value regardless of how the write was performed.

### Non-ThinkPad Lenovo models

ThinkBook, IdeaPad, and Legion models use different EC register layouts and are not
compatible. These are distinct product lines with different embedded controllers.

## EC Firmware Quirks

### Register 0x2F initialization bug

The EC does not correctly initialize register 0x2F on boot. The thinkpad_acpi driver
documentation notes that the fan control status cannot be determined until something writes
to the register for the first time. The ACPI DSDT methods typically handle this during
suspend/resume cycles.

Implication: on first run after boot, the initial read of 0x2F may not reflect the actual
fan state. The application should write 0x80 (BIOS mode) as an initialization step.

### Delayed response on newer models

Later ThinkPad models take at least 5 seconds to start responding to a fan mode change.
The polling loop should account for this -- do not treat a slow response as an EC failure.

### Dual-fan desync

On dual-fan models, the two fans may temporarily desync after a mode change. The protocol
(write fan 1 via register 0x31=0x00, sleep 100ms, write fan 2 via 0x31=0x01, sleep 100ms,
verify with readback) handles this, but retries (up to 5) may be needed.

## Linux Kernel FANG/FANW Regression (Informational)

Linux kernel 6.12 introduced new FANG/FANW ACPI methods for fan control that take precedence
over the legacy EC port method. On certain models (ThinkPad T495, T495s, E560), these ACPI
methods exist in the DSDT but don't actually work, breaking fan control entirely. A kernel
patch was submitted in March 2025 to add quirks for these models.

This does not affect this project (Windows, direct EC port access), but is documented here
for reference since these same models may exhibit unexpected ACPI behavior that could
interfere with Windows ACPI driver interactions.

## Determining Compatibility for Untested Models

To check if a ThinkPad model is compatible:

1. The model must have an ACPI EC device declaring I/O ports 0x62 and 0x66 in its DSDT.
   This is standard for all ThinkPads.

2. Register 0x2F must respond to reads and writes. A read should return a value with bit 7
   set (0x80+) when in BIOS mode.

3. Writing a manual level (0x00-0x07) to 0x2F should change the fan speed within ~5 seconds.

4. Writing 0x80 to 0x2F should return control to the BIOS.

If step 2 fails (read returns 0x00 or hangs), the model's EC firmware is likely incompatible.
If step 3 works but the value is quickly overridden, the model has the BIOS override behavior
described above.

## References

- ThinkWiki -- How to control fan speed: https://www.thinkwiki.org/wiki/How_to_control_fan_speed
- ThinkWiki -- Embedded Controller Firmware: https://www.thinkwiki.org/wiki/Embedded_Controller_Firmware
- TPFanCtrl2: https://github.com/Shuzhengz/TPFanCtrl2
- Linux thinkpad_acpi documentation: https://www.kernel.org/doc/Documentation/admin-guide/laptops/thinkpad-acpi.rst
- Linux tp_smapi thinkpad_ec.c: https://github.com/linux-thinkpad/tp_smapi/blob/master/thinkpad_ec.c
