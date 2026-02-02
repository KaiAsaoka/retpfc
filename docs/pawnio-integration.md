# PawnIO Integration

reTPFC uses PawnIO as its kernel-mode I/O driver instead of WinRing0.

## Why Not WinRing0

WinRing0 exposes arbitrary I/O port, MSR, and physical memory access to any usermode process.
It has a known privilege escalation vulnerability (CVE-2020-14979) and Microsoft Defender
actively flags `WinRing0x64.sys` as a vulnerable driver. It is on Microsoft's driver blocklist.

## What PawnIO Is

PawnIO is a signed kernel driver that runs sandboxed bytecode modules written in the Pawn
scripting language. Instead of exposing raw hardware access to userspace, each module defines
an allowlist of specific operations it permits. The driver is open source (GPLv2) with signed
binaries distributed at https://pawnio.eu.

Repositories:

- Driver source: https://github.com/namazso/PawnIO
- Usermode library: https://github.com/namazso/PawnIOLib
- Official modules: https://github.com/namazso/PawnIO.Modules

## Architecture

Three layers:

1. **PawnIO.sys** (kernel) -- Signed driver that loads and executes Pawn bytecode modules.
   Exposes a device at `\\.\PawnIO` with three IOCTLs: load module, execute function, query
   refcount.

2. **Pawn modules** (.bin files) -- Small scripts compiled to bytecode that run inside the
   driver. Each module exports named IOCTL functions and has access to native primitives
   (`io_in_byte`, `io_out_byte`, etc.). Modules define which hardware they can touch.

3. **PawnIOLib** (usermode) -- C/C++ library providing:
   - `pawnio_open()` -- open a handle to the driver
   - `pawnio_load(handle, binary, size)` -- load a compiled module
   - `pawnio_execute(handle, "function_name", in_params, in_count, out_params, out_count, &ret_size)` -- call a module function
   - `pawnio_close()` -- close the handle

   Parameters are passed as `ULONG64` arrays. Return values are `NTSTATUS` codes.
   Each function has three calling convention variants (COM/HRESULT, Win32/BOOL, NT/NTSTATUS).

## EC Access Module: LpcACPIEC

The existing signed module `LpcACPIEC` provides read/write access to the standard ACPI
Embedded Controller ports (0x62 and 0x66). This is the only module needed for this project.

The module exports two functions:

**ioctl_pio_read** -- Read a byte from an EC port.
- Input: `[port]` (1 element)
- Output: `[value]` (1 element)

**ioctl_pio_write** -- Write a byte to an EC port.
- Input: `[port, value]` (2 elements)
- Output: none

Both functions enforce a port allowlist (`0x62` and `0x66` only) and return
`STATUS_ACCESS_DENIED` for any other port. The module documentation notes that callers
should acquire the `\BaseNamedObjects\Access_EC` mutant before calling.

Source (complete module, ~50 lines of Pawn):

```pawn
is_port_allowed(port) {
    return port == 0x62 || port == 0x66;
}

DEFINE_IOCTL_SIZED(ioctl_pio_read, 1, 1) {
    new port = in[0] & 0xFFFF;
    if (!is_port_allowed(port))
        return STATUS_ACCESS_DENIED;
    out[0] = io_in_byte(port);
    return STATUS_SUCCESS;
}

DEFINE_IOCTL_SIZED(ioctl_pio_write, 2, 0) {
    new port = in[0] & 0xFFFF;
    new value = in[1];
    if (!is_port_allowed(port))
        return STATUS_ACCESS_DENIED;
    io_out_byte(port, value);
    return STATUS_SUCCESS;
}
```

## Integration With port_io.cpp

The EC handshake protocol (wait for IBF clear, send command, wait for OBF, read result)
is implemented in usermode C++. PawnIO only provides the raw port read/write. Usage:

```cpp
#include <PawnIOLib.h>

class PortIO {
    HANDLE hPawnIO = nullptr;

public:
    bool init() {
        if (FAILED(pawnio_open(&hPawnIO)))
            return false;
        auto binary = read_file("LpcACPIEC.bin");
        return SUCCEEDED(pawnio_load(hPawnIO, binary.data(), binary.size()));
    }

    uint8_t inb(uint16_t port) {
        ULONG64 in_params[] = { port };
        ULONG64 out_params[1] = {};
        ULONG out_size = 0;
        pawnio_execute(hPawnIO, "ioctl_pio_read",
                       in_params, 1, out_params, 1, &out_size);
        return static_cast<uint8_t>(out_params[0]);
    }

    void outb(uint16_t port, uint8_t value) {
        ULONG64 in_params[] = { port, value };
        pawnio_execute(hPawnIO, "ioctl_pio_write",
                       in_params, 2, nullptr, 0, nullptr);
    }

    void close() {
        if (hPawnIO) {
            pawnio_close(hPawnIO);
            hPawnIO = nullptr;
        }
    }
};
```

The rest of the EC protocol (reading temperatures, writing fan levels, dual-fan sequencing)
is built on top of `inb`/`outb` exactly as described in CLAUDE.md.

## Comparison With WinRing0 Approach

| Aspect              | WinRing0                          | PawnIO                                  |
|---------------------|-----------------------------------|-----------------------------------------|
| Driver loading      | `LoadLibrary("WinRing0x64.dll")`  | `pawnio_open()` + `pawnio_load()`       |
| Port read           | Function pointer to ReadIoPortByte| `pawnio_execute("ioctl_pio_read", ...)`  |
| Port write          | Function pointer to WriteIoPortByte| `pawnio_execute("ioctl_pio_write", ...)` |
| Cross-process mutex | `Access_Thinkpad_EC`              | `Access_EC`                             |
| Security model      | Any process can access any port   | Only ports 0x62/0x66 accessible         |
| Dependency          | Ship WinRing0 DLL + driver        | User installs PawnIO separately         |
| AV compatibility    | Flagged/blocked by Defender       | Signed, not blocked                     |

## Build Requirements

- PawnIOLib headers and library (from https://github.com/namazso/PawnIOLib)
- CMakeLists.txt must link against PawnIOLib
- The compiled `LpcACPIEC.bin` module must be distributed alongside the executable
  (signed builds available from PawnIO.Modules releases)

## User Requirements

- PawnIO driver must be installed (available from https://pawnio.eu or `winget install namazso.PawnIO`)
- Administrator privileges (required for EC access)

## Port Type Note

ThinkPad ECs historically used two port configurations:

| Type   | Control Port | Data Port | Used On           |
|--------|-------------|-----------|-------------------|
| Type 1 | 0x1604      | 0x1600    | Older ThinkPads (H8S LPC Channel 3) |
| Type 2 | 0x66        | 0x62      | Standard ACPI EC (all models)        |

The Type 1 ports (0x1600/0x1604) are a proprietary LPC Channel 3 interface used by the
Linux `tp_smapi` driver for features like battery threshold control. They are a different
communication channel, not an alternative path to the same EC registers.

All ThinkPad fan control (register 0x2F / HFSP) uses the standard ACPI EC interface at
ports 0x62/0x66. The LpcACPIEC module covers this. Type 1 port support is not needed.

If Type 1 support is ever required, the change to the Pawn module is a single line:

```pawn
is_port_allowed(port) {
    return port == 0x62 || port == 0x66 || port == 0x1600 || port == 0x1604;
}
```

This would need to be submitted as a PR to PawnIO.Modules and signed by namazso.
