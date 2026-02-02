/// @file main.cpp
/// @brief Entry point for reTPFC (retpfc.exe).
///
/// Currently implements Phase 1: loads and prints the INI config file
/// to verify the parser. Later phases will add EC access, fan control,
/// and the Win32 UI.

#include <iostream>
#include "config/config.h"

/// @brief Program entry point.
///
/// Loads a TPFanControl INI config file (default: TPFanControl.ini in the
/// working directory, or a path given as the first command-line argument)
/// and prints all parsed values to stdout.
/// @param argc Argument count.
/// @param argv Argument vector. Optional: argv[1] = path to INI file.
/// @return 0 on success, 1 if the config file cannot be loaded.
int main(int argc, char* argv[]) {
    std::string config_path = "TPFanControl.ini";
    if (argc > 1) {
        config_path = argv[1];
    }

    std::cout << "Loading config from: " << config_path << "\n\n";

    auto cfg = load_config(config_path);
    if (!cfg) {
        std::cerr << "Failed to load config file: " << config_path << "\n";
        return 1;
    }

    print_config(*cfg);
    return 0;
}
