cmake_minimum_required(VERSION 3.20)
project(hft_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_library(hft_warnings INTERFACE)
if(MSVC)
    target_compile_options(hft_warnings INTERFACE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hft_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion)
endif()

enable_testing()
add_subdirectory(common)
