#include "config/config.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// ---- Helper functions (static = only visible in this file) ----

/// @brief Trim whitespace from both ends of a string.
/// @param s The input string.
/// @return A new string with leading/trailing spaces, tabs, and newlines removed.
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

/// @brief Check if a line is a comment or empty.
///
/// Comment lines start with @c /, @c #, or @c ;.
/// @param line The trimmed line to check.
/// @return true if the line should be skipped during parsing.
static bool is_comment(const std::string& line) {
    if (line.empty()) return true;
    char first = line[0];
    return first == '/' || first == '#' || first == ';';
}

/// @brief Parse a fan curve line value into a FanLevel.
///
/// Expects space-separated integers: "TEMP FAN [HYST_UP] [HYST_DOWN]".
/// Hysteresis values are optional and default to 0.
/// @param value The value portion of a "Level=..." INI line (e.g. "60 3 2 0").
/// @return The parsed FanLevel, or @c std::nullopt if temperature and fan speed
///         cannot be read.
static std::optional<FanLevel> parse_fan_level(const std::string& value) {
    std::istringstream iss(value);
    FanLevel level{};

    // Temperature and fan speed are required
    if (!(iss >> level.temperature >> level.fan_speed)) {
        return std::nullopt;
    }

    // Hysteresis values are optional (default to 0)
    if (!(iss >> level.hysteresis_up)) level.hysteresis_up = 0;
    if (!(iss >> level.hysteresis_down)) level.hysteresis_down = 0;

    return level;
}

/// @brief Parse a hotkey line value into a HotkeyDef.
/// @param value The value portion of a "HK_*=..." INI line (e.g. "3 B").
/// @return The parsed hotkey, or @c std::nullopt if the modifier and key
///         cannot be read.
static std::optional<HotkeyDef> parse_hotkey(const std::string& value) {
    std::istringstream iss(value);
    int modifiers = 0;
    char key = 0;

    if (!(iss >> modifiers >> key)) {
        return std::nullopt;
    }

    return HotkeyDef{modifiers, key};
}

/// @brief Parse a comma-separated list into a vector of trimmed strings.
/// @param value The comma-separated input (e.g. "pci,aps" or "pci, aps").
/// @return Vector of non-empty trimmed tokens.
static std::vector<std::string> parse_comma_list(const std::string& value) {
    std::vector<std::string> result;
    std::istringstream iss(value);
    std::string token;

    while (std::getline(iss, token, ',')) {
        std::string trimmed = trim(token);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }

    return result;
}

/// @brief Parse space-separated integers into a vector.
/// @param value The space-separated input (e.g. "5 -1 -1").
/// @return Vector of parsed integers. Stops at the first non-integer token.
static std::vector<int> parse_int_list(const std::string& value) {
    std::vector<int> result;
    std::istringstream iss(value);
    int n;

    while (iss >> n) {
        result.push_back(n);
    }

    return result;
}

/// @brief Extract the sensor index from an indexed INI key name.
///
/// Keys like "SensorName1" or "SensorOffset4" have a trailing number that
/// identifies which sensor they apply to. The INI uses 1-based indexing;
/// this function converts to 0-based.
/// @param key The full key name (e.g. "SensorName1").
/// @return The 0-based sensor index, or -1 if no trailing digit is found.
static int extract_sensor_index(const std::string& key) {
    // Find where the digits start at the end
    auto pos = key.find_last_not_of("0123456789");
    if (pos == std::string::npos || pos == key.size() - 1) {
        return -1;  // No trailing digit
    }

    std::string digits = key.substr(pos + 1);
    int index = std::stoi(digits);

    // Config uses 1-based indexing, convert to 0-based
    return index - 1;
}

/// @brief Validate that a fan curve is properly formed.
///
/// A valid curve must:
/// - Have at least one entry.
/// - End with a terminator (temperature == -1).
/// - Have temperatures in strictly ascending order (excluding terminator).
/// @param curve The fan curve to validate.
/// @return true if the curve is valid.
static bool validate_fan_curve(const std::vector<FanLevel>& curve) {
    if (curve.empty()) return false;

    // Last entry must be the terminator (temperature == -1)
    if (curve.back().temperature != -1) return false;

    // Temperatures must be in ascending order (excluding terminator)
    for (size_t i = 1; i + 1 < curve.size(); ++i) {
        if (curve[i].temperature <= curve[i - 1].temperature) return false;
    }

    return true;
}

// ---- Public functions ----

/// @brief Parse a TPFanControl INI file into a Config struct.
///
/// Reads line by line, splits on @c =, and matches known keys. Unknown keys
/// are silently ignored. Fan curves are validated after parsing; malformed
/// curves produce a warning on stderr but do not cause the parse to fail.
/// @param filepath Path to the INI file.
/// @return The parsed configuration, or @c std::nullopt if the file cannot be opened.
std::optional<Config> load_config(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return std::nullopt;
    }

    Config cfg;
    std::string line;

    while (std::getline(file, line)) {
        line = trim(line);
        if (is_comment(line)) continue;

        // Split on first '=' to get key and value
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        // Match known keys
        if (key == "Active") {
            cfg.active_mode = static_cast<FanMode>(std::stoi(value));
        } else if (key == "Cycle") {
            cfg.cycle_seconds = std::stoi(value);
        } else if (key == "IconCycle") {
            cfg.icon_cycle_seconds = std::stoi(value);
        } else if (key == "ManFanSpeed") {
            cfg.manual_fan_speed = std::stoi(value);
        } else if (key == "ManModeExit") {
            cfg.manual_mode_exit_temp = std::stoi(value);
        } else if (key == "MaxReadErrors") {
            cfg.max_read_errors = std::stoi(value);
        } else if (key == "StartMinimized") {
            cfg.start_minimized = (std::stoi(value) != 0);
        } else if (key == "StayOnTop") {
            cfg.stay_on_top = (std::stoi(value) != 0);
        } else if (key == "SlimDialog") {
            cfg.slim_dialog = (std::stoi(value) != 0);
        } else if (key == "ShowTempIcon") {
            cfg.show_temp_icon = (std::stoi(value) != 0);
        } else if (key == "IconColorFan") {
            cfg.icon_color_fan = (std::stoi(value) != 0);
        } else if (key == "Log2File") {
            cfg.log_to_file = (std::stoi(value) != 0);
        } else if (key == "Log2csv") {
            cfg.log_to_csv = (std::stoi(value) != 0);
        }
        // Fan curve entries
        else if (key == "Level") {
            auto level = parse_fan_level(value);
            if (level) cfg.fan_curve.push_back(*level);
        } else if (key == "Level2") {
            auto level = parse_fan_level(value);
            if (level) cfg.fan_curve2.push_back(*level);
        }
        // Sensor configuration
        else if (key.rfind("SensorName", 0) == 0) {
            int idx = extract_sensor_index(key);
            if (idx >= 0) {
                // Grow vector if needed
                if (idx >= static_cast<int>(cfg.sensor_names.size())) {
                    cfg.sensor_names.resize(idx + 1);
                }
                cfg.sensor_names[idx] = value;
            }
        } else if (key.rfind("SensorOffset", 0) == 0) {
            int idx = extract_sensor_index(key);
            if (idx >= 0) {
                if (idx >= static_cast<int>(cfg.sensor_offsets.size())) {
                    cfg.sensor_offsets.resize(idx + 1);
                }
                cfg.sensor_offsets[idx] = parse_int_list(value);
            }
        } else if (key == "IgnoreSensors") {
            cfg.ignore_sensors = parse_comma_list(value);
        }
        // Hotkeys
        else if (key == "HK_BIOS") {
            cfg.hk_bios = parse_hotkey(value);
        } else if (key == "HK_Smart") {
            cfg.hk_smart = parse_hotkey(value);
        } else if (key == "HK_Manual") {
            cfg.hk_manual = parse_hotkey(value);
        }
        // Unknown keys are silently ignored
    }

    // Validate fan curves (warn but don't fail)
    if (!cfg.fan_curve.empty() && !validate_fan_curve(cfg.fan_curve)) {
        std::cerr << "Warning: primary fan curve is malformed\n";
    }
    if (!cfg.fan_curve2.empty() && !validate_fan_curve(cfg.fan_curve2)) {
        std::cerr << "Warning: secondary fan curve is malformed\n";
    }

    return cfg;
}

/// @brief Convert a FanMode enum value to a human-readable string.
/// @param mode The fan mode.
/// @return A string literal like "Smart" or "Manual".
static const char* fan_mode_str(FanMode mode) {
    switch (mode) {
        case FanMode::ReadOnly: return "ReadOnly";
        case FanMode::Disabled: return "Disabled";
        case FanMode::Smart:    return "Smart";
        case FanMode::Manual:   return "Manual";
        default:                return "Unknown";
    }
}

void print_config(const Config& cfg) {
    std::cout << "=== reTPFC Configuration ===\n\n";

    std::cout << "Mode:           " << fan_mode_str(cfg.active_mode) << "\n";
    std::cout << "Cycle:          " << cfg.cycle_seconds << "s\n";
    std::cout << "IconCycle:      " << cfg.icon_cycle_seconds << "s\n";
    std::cout << "ManFanSpeed:    " << cfg.manual_fan_speed << "\n";
    std::cout << "ManModeExit:    " << cfg.manual_mode_exit_temp << "C\n";
    std::cout << "MaxReadErrors:  " << cfg.max_read_errors << "\n";
    std::cout << "StartMinimized: " << cfg.start_minimized << "\n";
    std::cout << "StayOnTop:      " << cfg.stay_on_top << "\n";
    std::cout << "SlimDialog:     " << cfg.slim_dialog << "\n";
    std::cout << "ShowTempIcon:   " << cfg.show_temp_icon << "\n";
    std::cout << "IconColorFan:   " << cfg.icon_color_fan << "\n";
    std::cout << "Log2File:       " << cfg.log_to_file << "\n";
    std::cout << "Log2csv:        " << cfg.log_to_csv << "\n";

    // Print fan curve
    std::cout << "\n--- Fan Curve (primary) ---\n";
    if (cfg.fan_curve.empty()) {
        std::cout << "  (none)\n";
    } else {
        std::cout << "  Temp  Fan  HystUp  HystDown\n";
        for (const auto& level : cfg.fan_curve) {
            if (level.temperature == -1) {
                std::cout << "  [terminator]\n";
            } else {
                std::cout << "  " << level.temperature << "C"
                          << "    " << level.fan_speed
                          << "    " << level.hysteresis_up
                          << "       " << level.hysteresis_down << "\n";
            }
        }
    }

    if (!cfg.fan_curve2.empty()) {
        std::cout << "\n--- Fan Curve (secondary) ---\n";
        std::cout << "  Temp  Fan  HystUp  HystDown\n";
        for (const auto& level : cfg.fan_curve2) {
            if (level.temperature == -1) {
                std::cout << "  [terminator]\n";
            } else {
                std::cout << "  " << level.temperature << "C"
                          << "    " << level.fan_speed
                          << "    " << level.hysteresis_up
                          << "       " << level.hysteresis_down << "\n";
            }
        }
    }

    // Print sensor config
    if (!cfg.sensor_names.empty()) {
        std::cout << "\n--- Sensor Names ---\n";
        for (size_t i = 0; i < cfg.sensor_names.size(); ++i) {
            if (!cfg.sensor_names[i].empty()) {
                std::cout << "  Sensor " << (i + 1) << ": " << cfg.sensor_names[i] << "\n";
            }
        }
    }

    if (!cfg.sensor_offsets.empty()) {
        std::cout << "\n--- Sensor Offsets ---\n";
        for (size_t i = 0; i < cfg.sensor_offsets.size(); ++i) {
            if (!cfg.sensor_offsets[i].empty()) {
                std::cout << "  Sensor " << (i + 1) << ":";
                for (int offset : cfg.sensor_offsets[i]) {
                    std::cout << " " << offset;
                }
                std::cout << "\n";
            }
        }
    }

    if (!cfg.ignore_sensors.empty()) {
        std::cout << "\n--- Ignored Sensors ---\n";
        std::cout << "  ";
        for (size_t i = 0; i < cfg.ignore_sensors.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << cfg.ignore_sensors[i];
        }
        std::cout << "\n";
    }

    // Print hotkeys
    std::cout << "\n--- Hotkeys ---\n";
    auto print_hk = [](const char* name, const std::optional<HotkeyDef>& hk) {
        if (hk) {
            std::cout << "  " << name << ": modifiers=" << hk->modifiers
                      << " key=" << hk->key << "\n";
        }
    };
    print_hk("BIOS",   cfg.hk_bios);
    print_hk("Smart",  cfg.hk_smart);
    print_hk("Manual", cfg.hk_manual);
}
