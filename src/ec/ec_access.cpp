/// @file ec_access.cpp
/// @brief ThinkPad Embedded Controller access implementation.
///
/// Implements the EC read/write handshake protocol, temperature and RPM
/// reading, fan speed control, and cross-process synchronization via a
/// named Windows mutex.

#include "ec/ec_access.h"
#include "ec/port_io.h"

#include <chrono>
#include <thread>
#include <iostream>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace ec {

// ============================================================================
// Cross-process mutex
// ============================================================================
//
// The named mutex "Access_EC" prevents concurrent EC access from multiple
// processes (e.g., another fan control tool or BIOS-triggered EC access).

static HANDLE g_ec_mutex = nullptr;

/// @brief RAII guard for the EC cross-process mutex.
///
/// Acquires the named mutex on construction, releases on destruction.
/// If the mutex doesn't exist yet, creates it.
class ECLock {
public:
    ECLock() {
        if (!g_ec_mutex) {
            g_ec_mutex = CreateMutexW(nullptr, FALSE, L"Access_EC");
        }
        if (g_ec_mutex) {
            DWORD result = WaitForSingleObject(g_ec_mutex, TIMEOUT_MS);
            locked_ = (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED);
        }
    }

    ~ECLock() {
        if (locked_) ReleaseMutex(g_ec_mutex);
    }

    /// @brief Check if the mutex was successfully acquired.
    bool locked() const { return locked_; }

    ECLock(const ECLock&) = delete;
    ECLock& operator=(const ECLock&) = delete;

private:
    bool locked_ = false;
};

// ============================================================================
// Internal protocol functions (caller must hold the mutex)
// ============================================================================

/// @brief Wait for a specific EC status flag condition.
/// @param port The PortIO instance.
/// @param mask Bitmask to check against the status register.
/// @param expected The expected value after masking.
/// @return true if the condition was met before timeout.
static bool wait_status(PortIO& port, uint8_t mask, uint8_t expected) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(TIMEOUT_MS);

    while (std::chrono::steady_clock::now() < deadline) {
        auto status = port.inb(CTRL_PORT);
        if (!status) return false;
        if ((*status & mask) == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(POLL_INTERVAL_MS));
    }

    return false; // Timeout
}

/// @brief Read a byte from an EC register (no mutex).
///
/// Performs the full EC read handshake:
/// 1. Wait IBF clear
/// 2. Write CMD_READ to control port
/// 3. Wait IBF clear
/// 4. Write register offset to data port
/// 5. Wait OBF set
/// 6. Read result from data port
static std::optional<uint8_t> read_byte_impl(PortIO& port, uint8_t offset) {
    if (!wait_status(port, STATUS_IBF, 0x00)) return std::nullopt;
    if (!port.outb(CTRL_PORT, CMD_READ))      return std::nullopt;
    if (!wait_status(port, STATUS_IBF, 0x00)) return std::nullopt;
    if (!port.outb(DATA_PORT, offset))        return std::nullopt;
    if (!wait_status(port, STATUS_OBF, STATUS_OBF)) return std::nullopt;
    auto result = port.inb(DATA_PORT);

    // Brief settle time — the EC shares the bus with the ACPI driver
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return result;
}

/// @brief Write a byte to an EC register (no mutex).
///
/// Performs the full EC write handshake:
/// 1. Wait IBF clear
/// 2. Write CMD_WRITE to control port
/// 3. Wait IBF clear
/// 4. Write register offset to data port
/// 5. Wait IBF clear
/// 6. Write value to data port
static bool write_byte_impl(PortIO& port, uint8_t offset, uint8_t value) {
    if (!wait_status(port, STATUS_IBF, 0x00)) return false;
    if (!port.outb(CTRL_PORT, CMD_WRITE))     return false;
    if (!wait_status(port, STATUS_IBF, 0x00)) return false;
    if (!port.outb(DATA_PORT, offset))        return false;
    if (!wait_status(port, STATUS_IBF, 0x00)) return false;
    if (!port.outb(DATA_PORT, value))         return false;
    return true;
}

// ============================================================================
// Public API — simple operations (lock per call)
// ============================================================================

std::optional<uint8_t> read_byte(PortIO& port, uint8_t offset) {
    ECLock lock;
    if (!lock.locked()) return std::nullopt;
    return read_byte_impl(port, offset);
}

bool write_byte(PortIO& port, uint8_t offset, uint8_t value) {
    ECLock lock;
    if (!lock.locked()) return false;
    return write_byte_impl(port, offset, value);
}

std::optional<int> read_temperature(PortIO& port, uint8_t offset) {
    // Noise rejection: two consecutive reads must be within 2 degrees.
    // Exact match is too strict — active sensors update between reads.
    auto first = read_byte(port, offset);
    if (!first) return std::nullopt;

    auto second = read_byte(port, offset);
    if (!second) return std::nullopt;

    int a = static_cast<int>(*first);
    int b = static_cast<int>(*second);

    // Validate both: 0x00 and 0x80 are invalid, <10 or >127 is out of range
    if (a == 0 || a == TEMP_INVALID || a < TEMP_MIN || a > TEMP_MAX) return std::nullopt;
    if (b == 0 || b == TEMP_INVALID || b < TEMP_MIN || b > TEMP_MAX) return std::nullopt;

    int diff = (a > b) ? (a - b) : (b - a);
    if (diff > 2) return std::nullopt;

    // Return the average, biased toward the more recent read
    return b;
}

std::vector<std::optional<int>> read_all_temperatures(PortIO& port) {
    std::vector<std::optional<int>> temps;
    temps.reserve(SENSOR_COUNT);

    for (int i = 0; i < SENSOR_COUNT; ++i) {
        temps.push_back(read_temperature(port, SENSOR_OFFSETS[i]));
    }

    return temps;
}

std::optional<uint16_t> read_fan_rpm(PortIO& port) {
    // RPM registers can be flaky — retry up to 3 times
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto lo = read_byte(port, REG_RPM_LO);
        auto hi = read_byte(port, REG_RPM_HI);
        if (lo && hi) {
            uint16_t rpm = static_cast<uint16_t>((*hi << 8) | *lo);
            if (rpm == RPM_INVALID) return std::nullopt; // tachometer error
            return rpm;
        }
    }
    return std::nullopt;
}

bool set_bios_mode(PortIO& port) {
    return write_byte(port, REG_FAN_CTRL, FAN_BIOS_MODE);
}

bool set_fan_level(PortIO& port, uint8_t level) {
    if (level > 7) level = 7;
    return write_byte(port, REG_FAN_CTRL, level);
}

// ============================================================================
// Public API — compound operations (lock once for entire sequence)
// ============================================================================

bool set_fan_level_dual(PortIO& port, uint8_t level) {
    if (level > 7) level = 7;

    ECLock lock;
    if (!lock.locked()) return false;

    for (int attempt = 0; attempt < DUAL_FAN_RETRIES; ++attempt) {
        // Select Fan 1, set level
        if (!write_byte_impl(port, REG_FAN_SELECT, FAN_1)) return false;
        if (!write_byte_impl(port, REG_FAN_CTRL, level))   return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(DUAL_FAN_DELAY_MS));

        // Select Fan 2, set level
        if (!write_byte_impl(port, REG_FAN_SELECT, FAN_2)) return false;
        if (!write_byte_impl(port, REG_FAN_CTRL, level))   return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(DUAL_FAN_DELAY_MS));

        // Read back Fan 1
        if (!write_byte_impl(port, REG_FAN_SELECT, FAN_1)) return false;
        auto fan1 = read_byte_impl(port, REG_FAN_CTRL);

        // Read back Fan 2
        if (!write_byte_impl(port, REG_FAN_SELECT, FAN_2)) return false;
        auto fan2 = read_byte_impl(port, REG_FAN_CTRL);

        // Verify: mask off bit 7 (BIOS flag) and check level
        bool fan1_ok = fan1 && ((*fan1 & 0x7F) == level);
        bool fan2_ok = fan2 && ((*fan2 & 0x7F) == level);

        if (fan1_ok && fan2_ok) {
            // Restore Fan 1 as active selection
            write_byte_impl(port, REG_FAN_SELECT, FAN_1);
            return true;
        }
    }

    return false;
}

bool init(PortIO& port, bool& has_dual_fan) {
    has_dual_fan = false;

    // Test EC responsiveness by reading sensor 0 (CPU)
    auto test = read_byte(port, SENSOR_OFFSETS[0]);
    if (!test) {
        std::cerr << "EC not responding (failed to read sensor 0)\n";
        return false;
    }

    // Detect dual fan: select Fan 2 and read back
    ECLock lock;
    if (lock.locked()) {
        if (write_byte_impl(port, REG_FAN_SELECT, FAN_2)) {
            auto val = read_byte_impl(port, REG_FAN_SELECT);
            if (val && *val == FAN_2) {
                has_dual_fan = true;
            }
            // Restore Fan 1 selection
            write_byte_impl(port, REG_FAN_SELECT, FAN_1);
        }
    }

    return true;
}

} // namespace ec
