#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

/// @brief One row of the fan curve table.
///
/// Maps a temperature threshold to a fan speed level, with optional
/// hysteresis to prevent rapid on/off cycling near boundaries.
///
/// Parsed from INI lines like: @c Level=60 3 2 0
struct FanLevel {
    int temperature;      ///< Activation temperature in Celsius. -1 = terminator.
    int fan_speed;        ///< EC fan level (0-7). 0 = off, 7 = max.
    int hysteresis_up;    ///< Extra degrees above @c temperature needed to activate.
    int hysteresis_down;  ///< Extra degrees below @c temperature needed to deactivate.
};

/// @brief Operating mode for the fan controller.
///
/// Corresponds to the INI @c Active= setting (0-3).
enum class FanMode {
    ReadOnly = 0,   ///< Monitor temperatures only, never write to EC.
    Disabled = 1,   ///< Fan control off, application idle.
    Smart    = 2,   ///< Automatic control via fan curve lookup.
    Manual   = 3    ///< User-specified fixed fan level.
};

/// @brief Global keyboard shortcut definition.
///
/// Parsed from INI lines like: @c HK_BIOS=3 B (Ctrl+Shift+B).
/// Registered with the Win32 RegisterHotKey API.
struct HotkeyDef {
    int modifiers;    ///< Modifier combo: 1=Ctrl, 2=Shift, 3=Ctrl+Shift, 4=Alt, 5=Alt+Shift.
    char key;         ///< The key character (uppercase).
};

/// @brief All parsed configuration values from TPFanControl.ini.
///
/// Every field has a default value, so missing INI keys produce sane defaults.
struct Config {
    FanMode active_mode = FanMode::Smart;  ///< Fan controller operating mode.
    int cycle_seconds = 5;                 ///< EC temperature poll interval in seconds.
    int icon_cycle_seconds = 1;            ///< Tray icon update interval in seconds.
    int manual_fan_speed = 0;              ///< Default fan level for manual mode (0-7).
    int manual_mode_exit_temp = 78;        ///< Auto-exit manual mode above this temp (Celsius).
    int max_read_errors = 10;              ///< Consecutive EC read failures before BIOS fallback.
    bool start_minimized = true;           ///< Start hidden in the system tray.
    bool stay_on_top = true;               ///< Keep settings window above other windows.
    bool slim_dialog = false;              ///< Use compact UI layout.
    bool show_temp_icon = true;            ///< true = text icon, false = color icon.
    bool icon_color_fan = true;            ///< Tint tray icon green when fan is active.
    bool log_to_file = false;              ///< Write debug log to file.
    bool log_to_csv = false;               ///< Write temperature/fan data to CSV.

    std::vector<FanLevel> fan_curve;           ///< Primary fan curve ("Level=..." entries).
    std::vector<FanLevel> fan_curve2;          ///< Secondary fan curve ("Level2=..." entries).

    std::vector<std::string> sensor_names;     ///< Custom sensor names, indexed by sensor number.
    std::vector<std::vector<int>> sensor_offsets; ///< Temperature offsets per sensor (Celsius).
    std::vector<std::string> ignore_sensors;   ///< Sensor names excluded from max-temp calculation.

    std::optional<HotkeyDef> hk_bios;    ///< Hotkey to switch to BIOS mode.
    std::optional<HotkeyDef> hk_smart;   ///< Hotkey to switch to Smart mode.
    std::optional<HotkeyDef> hk_manual;  ///< Hotkey to switch to Manual mode.
};

/// @brief Parse a TPFanControl INI file into a Config struct.
/// @param filepath Path to the INI file (absolute or relative to working directory).
/// @return The parsed configuration, or @c std::nullopt if the file cannot be opened.
std::optional<Config> load_config(const std::string& filepath);

/// @brief Print all parsed config values to stdout.
///
/// Used for console testing during Phase 1. Dumps every field in a
/// human-readable format to verify the parser works correctly.
/// @param cfg The configuration to print.
void print_config(const Config& cfg);
