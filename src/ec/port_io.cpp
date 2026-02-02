/// @file port_io.cpp
/// @brief Low-level I/O port access implementation.
///
/// Contains two implementations selected at compile time:
/// - Real mode: Uses PawnIO driver and LpcACPIEC module for hardware access.
/// - Mock mode (RETPFC_MOCK_EC): Simulates the EC state machine for
///   development and testing without hardware.

#include "ec/port_io.h"

#include <iostream>

#ifdef RETPFC_MOCK_EC

// ============================================================================
// Mock EC implementation
// ============================================================================
//
// Simulates the EC's port-level behavior so ec_access.cpp can exercise its
// real handshake protocol without hardware. The mock tracks a simple state
// machine matching the EC's read/write command sequences.

#include <unordered_map>

/// @brief States of the mock EC command state machine.
enum class MockState {
    IDLE,           ///< Ready for a new command on the control port.
    READ_OFFSET,    ///< Expecting register offset on data port (read).
    READ_DATA,      ///< Data ready to be read from data port.
    WRITE_OFFSET,   ///< Expecting register offset on data port (write).
    WRITE_DATA      ///< Expecting data value on data port (write).
};

static MockState g_state = MockState::IDLE;
static uint8_t g_current_offset = 0;

/// @brief Simulated EC register contents.
static std::unordered_map<uint8_t, uint8_t> g_mock_regs = {
    // Temperature sensors (Celsius)
    {0x78, 55},   // cpu
    {0x79, 0x80}, // aps (invalid)
    {0x7A, 48},   // crd
    {0x7B, 60},   // gpu
    {0x7C, 42},   // bat
    {0x7D, 0x80}, // x7d (invalid)
    {0x7E, 40},   // bat2
    {0x7F, 0x80}, // x7f (invalid)
    {0xC0, 50},   // bus
    {0xC1, 45},   // pci
    {0xC2, 38},   // pwr
    {0xC3, 0x80}, // xc3 (invalid)
    // Fan RPM (little-endian: 0x09A0 = 2464 RPM)
    {0x84, 0xA0},
    {0x85, 0x09},
    // Fan control
    {0x2F, 0x80}, // BIOS mode
    {0x31, 0x00}, // Fan 1 selected
};

PortIO::~PortIO() {
    shutdown();
}

bool PortIO::init(const std::string& module_path) {
    (void)module_path;
    std::cerr << "[MOCK EC] Initialized (no real hardware access)\n";
    open_ = true;
    return true;
}

void PortIO::shutdown() {
    if (open_) {
        std::cerr << "[MOCK EC] Shutdown\n";
    }
    open_ = false;
}

std::optional<uint8_t> PortIO::inb(uint16_t port) {
    if (!open_) return std::nullopt;

    if (port == 0x66) {
        // Status register
        if (g_state == MockState::READ_DATA) {
            return 0x01; // OBF set — data ready to read
        }
        return 0x00; // IBF clear — ready for input
    }

    if (port == 0x62) {
        // Data register
        if (g_state == MockState::READ_DATA) {
            uint8_t val = 0x80; // default invalid
            auto it = g_mock_regs.find(g_current_offset);
            if (it != g_mock_regs.end()) val = it->second;
            g_state = MockState::IDLE;
            return val;
        }
    }

    return 0x00;
}

bool PortIO::outb(uint16_t port, uint8_t value) {
    if (!open_) return false;

    if (port == 0x66) {
        // Command register
        if (value == 0x80) {
            g_state = MockState::READ_OFFSET;
        } else if (value == 0x81) {
            g_state = MockState::WRITE_OFFSET;
        }
    } else if (port == 0x62) {
        // Data register
        switch (g_state) {
            case MockState::READ_OFFSET:
                g_current_offset = value;
                g_state = MockState::READ_DATA;
                break;
            case MockState::WRITE_OFFSET:
                g_current_offset = value;
                g_state = MockState::WRITE_DATA;
                break;
            case MockState::WRITE_DATA:
                g_mock_regs[g_current_offset] = value;
                std::cerr << "[MOCK EC] Write reg 0x" << std::hex
                          << static_cast<int>(g_current_offset) << " = 0x"
                          << static_cast<int>(value) << std::dec << "\n";
                g_state = MockState::IDLE;
                break;
            default:
                break;
        }
    }

    return true;
}

bool PortIO::is_open() const {
    return open_;
}

#else // !RETPFC_MOCK_EC

// ============================================================================
// Real PawnIO implementation
// ============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winternl.h>
#include <PawnIOLib.h>

#include <fstream>
#include <vector>

PortIO::~PortIO() {
    shutdown();
}

bool PortIO::init(const std::string& module_path) {
    HANDLE h = nullptr;
    HRESULT hr = pawnio_open(&h);
    if (FAILED(hr)) {
        std::cerr << "Failed to open PawnIO driver (HRESULT=0x"
                  << std::hex << hr << std::dec << ")\n"
                  << "Is the PawnIO driver installed and running as admin?\n";
        return false;
    }
    handle_ = h;

    // Read the module file
    std::ifstream file(module_path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Failed to open module file: " << module_path << "\n";
        shutdown();
        return false;
    }

    auto size = file.tellg();
    file.seekg(0);
    std::vector<unsigned char> blob(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(blob.data()), size);

    if (!file) {
        std::cerr << "Failed to read module file: " << module_path << "\n";
        shutdown();
        return false;
    }

    // Load the module into PawnIO
    hr = pawnio_load(static_cast<HANDLE>(handle_), blob.data(), blob.size());
    if (FAILED(hr)) {
        std::cerr << "Failed to load PawnIO module (HRESULT=0x"
                  << std::hex << hr << std::dec << ")\n";
        shutdown();
        return false;
    }

    open_ = true;
    return true;
}

void PortIO::shutdown() {
    if (handle_) {
        pawnio_close(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
    open_ = false;
}

std::optional<uint8_t> PortIO::inb(uint16_t port) {
    if (!open_) return std::nullopt;

    ULONG64 in_params[] = {port};
    ULONG64 out_params[1] = {};
    SIZE_T return_size = 0;

    HRESULT hr = pawnio_execute(
        static_cast<HANDLE>(handle_),
        "ioctl_pio_read",
        in_params, 1,
        out_params, 1,
        &return_size
    );

    if (FAILED(hr)) return std::nullopt;
    return static_cast<uint8_t>(out_params[0]);
}

bool PortIO::outb(uint16_t port, uint8_t value) {
    if (!open_) return false;

    ULONG64 in_params[] = {port, value};
    SIZE_T return_size = 0;

    HRESULT hr = pawnio_execute(
        static_cast<HANDLE>(handle_),
        "ioctl_pio_write",
        in_params, 2,
        nullptr, 0,
        &return_size
    );

    return SUCCEEDED(hr);
}

bool PortIO::is_open() const {
    return open_;
}

#endif // RETPFC_MOCK_EC
