/// @file ec_access.h
/// @brief ThinkPad Embedded Controller access layer.
///
/// Defines all EC register constants, protocol timing, and the public API
/// for reading temperatures, fan RPM, and controlling fan speed.
/// Built on top of the PortIO low-level I/O abstraction.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <array>

class PortIO;

/// @brief ThinkPad Embedded Controller access functions and constants.
namespace ec {

// ============================================================================
// EC I/O Ports (standard ACPI EC)
// ============================================================================

constexpr uint16_t DATA_PORT = 0x62;  ///< EC data register (read/write).
constexpr uint16_t CTRL_PORT = 0x66;  ///< EC command/status register.

// ============================================================================
// Status Register Flags (read from CTRL_PORT)
// ============================================================================

constexpr uint8_t STATUS_OBF = 0x01;  ///< Output buffer full — data ready to read.
constexpr uint8_t STATUS_IBF = 0x02;  ///< Input buffer full — EC busy, wait before writing.

// ============================================================================
// Commands (written to CTRL_PORT)
// ============================================================================

constexpr uint8_t CMD_READ  = 0x80;  ///< Read a byte from an EC register.
constexpr uint8_t CMD_WRITE = 0x81;  ///< Write a byte to an EC register.

// ============================================================================
// Fan Control Registers
// ============================================================================

constexpr uint8_t REG_FAN_CTRL   = 0x2F;  ///< Fan speed control. Bit 7 = BIOS mode.
constexpr uint8_t REG_FAN_SELECT = 0x31;  ///< Fan selector (0x00 = Fan 1, 0x01 = Fan 2).
constexpr uint8_t FAN_BIOS_MODE  = 0x80;  ///< Write to REG_FAN_CTRL to let EC firmware control fan.
constexpr uint8_t FAN_1 = 0x00;           ///< Fan selector value for Fan 1.
constexpr uint8_t FAN_2 = 0x01;           ///< Fan selector value for Fan 2.

// ============================================================================
// Fan RPM Registers
// ============================================================================

constexpr uint8_t REG_RPM_LO = 0x84;  ///< Fan RPM low byte.
constexpr uint8_t REG_RPM_HI = 0x85;  ///< Fan RPM high byte. RPM = (HI << 8) | LO.

// ============================================================================
// Temperature Sensors
// ============================================================================

constexpr int SENSOR_COUNT = 12;  ///< Total number of EC temperature sensors.

/// @brief EC register offsets for each temperature sensor.
constexpr std::array<uint8_t, SENSOR_COUNT> SENSOR_OFFSETS = {
    0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,  // Group 1
    0xC0, 0xC1, 0xC2, 0xC3                              // Group 2
};

/// @brief Default 3-character names for each sensor.
constexpr std::array<const char*, SENSOR_COUNT> DEFAULT_SENSOR_NAMES = {
    "cpu", "aps", "crd", "gpu", "bat", "x7d", "bat", "x7f",
    "bus", "pci", "pwr", "xc3"
};

// ============================================================================
// Timing Constants
// ============================================================================

constexpr int TIMEOUT_MS       = 1000;  ///< Max time to wait for an EC status flag.
constexpr int POLL_INTERVAL_MS = 10;    ///< Interval between status flag polls.
constexpr int DUAL_FAN_DELAY_MS = 100;  ///< Delay between dual-fan write sequences.
constexpr int DUAL_FAN_RETRIES  = 5;    ///< Max retries for dual-fan verification.

// ============================================================================
// Temperature Validation
// ============================================================================

constexpr uint8_t TEMP_INVALID = 0x80;  ///< Sensor value indicating inactive/invalid.
constexpr int     TEMP_MIN     = 10;    ///< Minimum plausible temperature (rejects ghost sensors).
constexpr int     TEMP_MAX     = 127;   ///< Maximum valid temperature reading.
constexpr uint16_t RPM_INVALID = 0xFFFF; ///< Tachometer error code (no valid reading).

// ============================================================================
// Public API
// ============================================================================

/// @brief Check if the EC is responsive and detect dual-fan configuration.
/// @param port The PortIO instance to use.
/// @param[out] has_dual_fan Set to true if a second fan is detected.
/// @return true if the EC responds to a test read.
bool init(PortIO& port, bool& has_dual_fan);

/// @brief Read a byte from an EC register.
/// @param port The PortIO instance to use.
/// @param offset The EC register offset to read.
/// @return The byte read, or @c std::nullopt on timeout or failure.
std::optional<uint8_t> read_byte(PortIO& port, uint8_t offset);

/// @brief Write a byte to an EC register.
/// @param port The PortIO instance to use.
/// @param offset The EC register offset to write.
/// @param value The byte to write.
/// @return true on success, false on timeout or failure.
bool write_byte(PortIO& port, uint8_t offset, uint8_t value);

/// @brief Read a temperature sensor with validation and noise rejection.
///
/// Performs two consecutive reads and requires matching values (noise
/// rejection). Rejects values of 0x00, 0x80, and anything above 127.
/// @param port The PortIO instance to use.
/// @param offset The sensor register offset.
/// @return Temperature in Celsius, or @c std::nullopt if invalid.
std::optional<int> read_temperature(PortIO& port, uint8_t offset);

/// @brief Read all 12 temperature sensors.
/// @param port The PortIO instance to use.
/// @return Vector of 12 optional temperature values (Celsius).
std::vector<std::optional<int>> read_all_temperatures(PortIO& port);

/// @brief Read the current fan speed in RPM.
/// @param port The PortIO instance to use.
/// @return Fan RPM, or @c std::nullopt on failure.
std::optional<uint16_t> read_fan_rpm(PortIO& port);

/// @brief Set fan to BIOS mode (EC firmware controls fan).
///
/// Writes 0x80 to the fan control register. This is the safest mode
/// and should always be set on program exit.
/// @param port The PortIO instance to use.
/// @return true on success.
bool set_bios_mode(PortIO& port);

/// @brief Set fan to a specific speed level.
/// @param port The PortIO instance to use.
/// @param level Fan speed level (0-7). Clamped to 7 if higher.
/// @return true on success.
bool set_fan_level(PortIO& port, uint8_t level);

/// @brief Set fan level on dual-fan systems.
///
/// Writes to both Fan 1 and Fan 2 with the dual-fan protocol
/// (select fan, write level, delay, repeat). Verifies by reading back
/// and retries up to DUAL_FAN_RETRIES times on mismatch.
/// @param port The PortIO instance to use.
/// @param level Fan speed level (0-7). Clamped to 7 if higher.
/// @return true if both fans confirmed the level.
bool set_fan_level_dual(PortIO& port, uint8_t level);

} // namespace ec
