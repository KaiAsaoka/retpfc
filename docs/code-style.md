# Code Style — Doxygen Documentation

All C++ code in this project uses Doxygen-style docstrings with `@`-tags.

## What Must Be Documented

- All public functions (declared in `.h` files)
- All structs, enums, and enum values
- All struct/class members
- Static helper functions in `.cpp` files
- File-level `@file` blocks for files with non-obvious purpose

## Tag Reference

### @brief — One-line summary

Every documented item needs a `@brief`. This is the short description that shows up in
generated docs and IDE tooltips.

```cpp
/// @brief Parse a TPFanControl INI file into a Config struct.
std::optional<Config> load_config(const std::string& filepath);
```

### @param — Function parameters

One `@param` per parameter. Describe what the caller should pass, not the implementation
detail.

```cpp
/// @brief Trim whitespace from both ends of a string.
/// @param s The input string.
/// @return A new string with leading/trailing whitespace removed.
static std::string trim(const std::string& s);
```

### @return — Return value

Describe what the function returns, including failure cases.

```cpp
/// @return The parsed configuration, or @c std::nullopt if the file cannot be opened.
```

### @c — Inline code

Marks the next word as code (renders in monospace). Use for type names, values, and
INI key references.

```cpp
/// @return The parsed FanLevel, or @c std::nullopt on failure.
/// Parsed from INI lines like: @c Level=60 3 2 0
```

### ///< — Inline member documentation

For struct/enum members, place the doc on the same line after the member. This keeps
structs compact and readable.

```cpp
struct FanLevel {
    int temperature;      ///< Activation temperature in Celsius. -1 = terminator.
    int fan_speed;        ///< EC fan level (0-7). 0 = off, 7 = max.
};
```

### @file — File documentation

Use at the top of a `.cpp` file when the file's purpose isn't obvious from its name.

```cpp
/// @file main.cpp
/// @brief Entry point for TPFanCtrl.
///
/// Currently implements Phase 1: loads and prints the INI config file
/// to verify the parser.
```

### @warning — Safety notes

Use for hardware safety concerns or critical usage requirements.

```cpp
/// @warning You should acquire the Access_EC mutex before calling this.
```

## Multi-line Descriptions

Separate `@brief` from the detailed description with a blank `///` line:

```cpp
/// @brief Validate that a fan curve is properly formed.
///
/// A valid curve must:
/// - Have at least one entry.
/// - End with a terminator (temperature == -1).
/// - Have temperatures in strictly ascending order (excluding terminator).
/// @param curve The fan curve to validate.
/// @return true if the curve is valid.
static bool validate_fan_curve(const std::vector<FanLevel>& curve);
```

The blank line tells Doxygen where the brief ends and the detailed description begins.

## Enum Documentation

Document the enum itself with `@brief`, and each value with `///<`:

```cpp
/// @brief Operating mode for the fan controller.
///
/// Corresponds to the INI @c Active= setting (0-3).
enum class FanMode {
    ReadOnly = 0,   ///< Monitor temperatures only, never write to EC.
    Disabled = 1,   ///< Fan control off, application idle.
    Smart    = 2,   ///< Automatic control via fan curve lookup.
    Manual   = 3    ///< User-specified fixed fan level.
};
```

## Static Functions

Static (file-private) helper functions in `.cpp` files should be documented the same way
as public functions. These are internal, but future contributors still need to understand
them.

```cpp
/// @brief Extract the sensor index from an indexed INI key name.
///
/// Keys like "SensorName1" have a trailing number identifying the sensor.
/// The INI uses 1-based indexing; this function converts to 0-based.
/// @param key The full key name (e.g. "SensorName1").
/// @return The 0-based sensor index, or -1 if no trailing digit is found.
static int extract_sensor_index(const std::string& key);
```

## What NOT to Document

- Obvious one-liner implementations (e.g. simple getters)
- Individual local variables inside function bodies
- Closing braces or namespace ends
- Do not add `@author` or `@date` tags (git handles this)
