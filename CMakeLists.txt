cmake_minimum_required(VERSION 3.20)
project(retpfc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(retpfc
    src/main.cpp
    src/config/config.cpp
)
target_include_directories(retpfc PRIVATE src)
