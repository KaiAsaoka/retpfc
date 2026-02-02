/// @file main.cpp
/// @brief Entry point for reTPFC (retpfc.exe).
///
/// Phase 1: Loads and prints the INI config file.
/// Phase 2: Initializes EC access via PawnIO, reads all temperature sensors
/// and fan RPM, and prints a sensor table. Sets BIOS mode on exit for safety.

#include <iostream>
#include <string>
#include <csignal>
#include <cstdlib>
#include <iomanip>

#include "config/config.h"
#include "ec/port_io.h"
#include "ec/ec_access.h"

/// @brief Global PortIO pointer for cleanup in atexit/signal handlers.
static PortIO* g_port = nullptr;

/// @brief Restore BIOS fan control mode on normal program exit.
static void cleanup_atexit() {
    if (g_port && g_port->is_open()) {
        ec::set_bios_mode(*g_port);
        g_port->shutdown();
    }
}

/// @brief Restore BIOS fan control mode on signal (Ctrl+C, termination).
/// @param sig The signal number.
static void cleanup_signal(int sig) {
    if (g_port && g_port->is_open()) {
        ec::set_bios_mode(*g_port);
        g_port->shutdown();
    }
    std::_Exit(128 + sig);
}

/// @brief Print the sensor table header and readings.
static void print_sensor_table(
    PortIO& port,
    const std::vector<std::optional<int>>& temps,
    const Config& cfg
) {
    std::cout << "\n=== EC Sensor Readings ===\n\n";
    std::cout << std::left
              << std::setw(10) << "Sensor"
              << std::setw(8)  << "Name"
              << std::setw(8)  << "Temp"
              << "\n";
    std::cout << std::string(26, '-') << "\n";

    for (int i = 0; i < ec::SENSOR_COUNT; ++i) {
        // Sensor number (1-based)
        std::cout << std::left << std::setw(10) << (i + 1);

        // Sensor name: use config override if available, else default
        const char* name = ec::DEFAULT_SENSOR_NAMES[i];
        if (i < static_cast<int>(cfg.sensor_names.size())
            && !cfg.sensor_names[i].empty()) {
            std::cout << std::setw(8) << cfg.sensor_names[i];
        } else {
            std::cout << std::setw(8) << name;
        }

        // Temperature
        if (temps[i]) {
            std::cout << *temps[i] << "C";
        } else {
            std::cout << "--";
        }
        std::cout << "\n";
    }

    // Fan RPM
    auto rpm = ec::read_fan_rpm(port);
    std::cout << "\nFan RPM: ";
    if (rpm) {
        std::cout << *rpm << "\n";
    } else {
        std::cout << "-- (read failed)\n";
    }
}

/// @brief Program entry point.
/// @param argc Argument count.
/// @param argv Argument vector. Optional: argv[1] = path to INI file.
/// @return 0 on success, 1 on config/EC failure.
int main(int argc, char* argv[]) {
    std::string config_path = "TPFanControl.ini";
    if (argc > 1) {
        config_path = argv[1];
    }

    // --- Phase 1: Load config ---

    std::cout << "Loading config from: " << config_path << "\n\n";

    auto cfg = load_config(config_path);
    if (!cfg) {
        std::cerr << "Failed to load config file: " << config_path << "\n";
        return 1;
    }

    print_config(*cfg);

    // --- Phase 2: EC access ---

    PortIO port;
    g_port = &port;

    // Register cleanup handlers
    std::atexit(cleanup_atexit);
    std::signal(SIGINT, cleanup_signal);
    std::signal(SIGTERM, cleanup_signal);

    // Initialize PawnIO and load the EC access module
    std::string module_path = "LpcACPIEC.bin";
    if (!port.init(module_path)) {
        std::cerr << "Failed to initialize EC access.\n";
        return 1;
    }

    // Check EC responsiveness and detect dual-fan
    bool has_dual_fan = false;
    if (!ec::init(port, has_dual_fan)) {
        std::cerr << "EC initialization failed.\n";
        return 1;
    }

    std::cout << "\nEC initialized successfully.";
    if (has_dual_fan) {
        std::cout << " Dual-fan system detected.";
    }
    std::cout << "\n";

    // Read and display all temperatures
    auto temps = ec::read_all_temperatures(port);
    print_sensor_table(port, temps, *cfg);

    // Read current fan control register
    auto fan_ctrl = ec::read_byte(port, ec::REG_FAN_CTRL);
    if (fan_ctrl) {
        std::cout << "\nFan control register: 0x"
                  << std::hex << std::setfill('0') << std::setw(2)
                  << static_cast<int>(*fan_ctrl)
                  << std::dec << std::setfill(' ');
        if (*fan_ctrl & 0x80) {
            std::cout << " (BIOS mode)";
        } else {
            std::cout << " (manual level " << (*fan_ctrl & 0x7F) << ")";
        }
        std::cout << "\n";
    }

    // Restore BIOS mode on exit (atexit handler also does this as a safety net)
    ec::set_bios_mode(port);
    std::cout << "\nBIOS fan mode restored. Exiting.\n";

    g_port = nullptr;
    port.shutdown();
    return 0;
}
