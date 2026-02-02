/// @file port_io.h
/// @brief Low-level I/O port access via PawnIO driver.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

/// @brief RAII wrapper for low-level I/O port access via PawnIO.
///
/// Manages the PawnIO driver handle and loaded LpcACPIEC module.
/// Provides inb/outb primitives that the EC protocol layer builds on.
///
/// In mock mode (RETPFC_MOCK_EC), simulates EC hardware for development
/// without requiring the actual PawnIO driver or admin privileges.
class PortIO {
public:
    PortIO() = default;
    ~PortIO();

    PortIO(const PortIO&) = delete;
    PortIO& operator=(const PortIO&) = delete;

    /// @brief Initialize the PawnIO driver and load the EC access module.
    /// @param module_path Path to the LpcACPIEC.amx module file.
    /// @return true on success, false if the driver or module failed to load.
    bool init(const std::string& module_path);

    /// @brief Shut down the PawnIO driver and release resources.
    void shutdown();

    /// @brief Read a byte from an I/O port.
    /// @param port The port address (0x62 or 0x66 for EC access).
    /// @return The byte read, or @c std::nullopt on failure.
    std::optional<uint8_t> inb(uint16_t port);

    /// @brief Write a byte to an I/O port.
    /// @param port The port address (0x62 or 0x66 for EC access).
    /// @param value The byte to write.
    /// @return true on success, false on failure.
    bool outb(uint16_t port, uint8_t value);

    /// @brief Check if the driver is initialized and ready.
    /// @return true if init() succeeded and shutdown() hasn't been called.
    bool is_open() const;

private:
    void* handle_ = nullptr;  ///< PawnIO driver handle (HANDLE stored as void*).
    bool open_ = false;        ///< Whether init() succeeded.
};
