cmake_minimum_required(VERSION 3.20)
project(retpfc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Option to build with mock EC (no hardware access, no PawnIO dependency)
option(RETPFC_MOCK_EC "Use mock EC for development without hardware" OFF)

add_executable(retpfc
    src/main.cpp
    src/config/config.cpp
    src/ec/port_io.cpp
    src/ec/ec_access.cpp
)
target_include_directories(retpfc PRIVATE src)

if(RETPFC_MOCK_EC)
    target_compile_definitions(retpfc PRIVATE RETPFC_MOCK_EC)
    message(STATUS "Building with MOCK EC (no real hardware access)")
else()
    # Compile PawnIOLib source directly into the executable (avoids shipping a separate DLL).
    # PawnIOLib_EXPORTS makes the export macros resolve to __declspec(dllexport),
    # which is harmless in an exe (symbols appear in the export table but are never imported).
    target_sources(retpfc PRIVATE
        thirdparty/PawnIOLib/PawnIOLib/PawnIOLib.cpp
    )
    target_compile_definitions(retpfc PRIVATE PawnIOLib_EXPORTS)
    target_include_directories(retpfc PRIVATE
        thirdparty/PawnIOLib/PawnIOLib          # For ioctl.h (used by PawnIOLib.cpp)
        thirdparty/PawnIOLib/PawnIOLib/public   # For PawnIOLib.h (used by port_io.cpp)
    )
    target_link_libraries(retpfc PRIVATE ntdll.lib)
    message(STATUS "Building with PawnIO EC access")
endif()
