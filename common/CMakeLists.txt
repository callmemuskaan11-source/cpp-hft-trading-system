
add_library(hft_common INTERFACE)

target_include_directories(hft_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(hft_common INTERFACE cxx_std_20)

add_executable(test_common
    tests/test_common.cpp
)

target_link_libraries(test_common PRIVATE hft_common hft_warnings)

add_test(NAME test_common COMMAND test_common)
