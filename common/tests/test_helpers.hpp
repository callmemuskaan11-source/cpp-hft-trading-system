#pragma once

#include <iostream>

namespace test {

inline int failures = 0;

inline void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        ++failures;
        std::cerr << file << ':' << line << ": CHECK failed: " << expression << '\n';
    }
}

} // namespace test

#define CHECK(condition) \
    ::test::check(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define CHECK_EQ(a, b) \
    ::test::check((a) == (b), #a " == " #b, __FILE__, __LINE__)
