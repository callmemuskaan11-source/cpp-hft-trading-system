add_library(hft_market_data
    src/synthetic_generator.cpp
    src/event_writer.cpp
    src/replay_reader.cpp
    src/limit_order_book.cpp
)
target_include_directories(hft_market_data PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hft_market_data PUBLIC hft_common PRIVATE hft_warnings)

foreach(test_target IN ITEMS test_synthetic_generator test_event_serialization test_limit_order_book)
    add_executable(${test_target} tests/${test_target}.cpp)
    target_include_directories(${test_target} PRIVATE ${PROJECT_SOURCE_DIR}/common/tests)
    target_link_libraries(${test_target} PRIVATE hft_market_data hft_warnings)
    add_test(NAME ${test_target} COMMAND ${test_target})
endforeach()
