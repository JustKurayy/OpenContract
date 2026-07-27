#pragma once

#include <exception>
#include <iostream>
#include <string_view>

namespace contract::test {

inline int failures = 0;

inline void expect(bool condition, std::string_view expression, std::string_view file, int line) {
    if (!condition) {
        std::cerr << file << ':' << line << ": expectation failed: " << expression << '\n';
        ++failures;
    }
}

template <typename Actual, typename Expected>
void expect_equal(
    const Actual& actual,
    const Expected& expected,
    std::string_view expression,
    std::string_view file,
    int line) {
    if (!(actual == expected)) {
        std::cerr << file << ':' << line << ": equality failed: " << expression << '\n';
        ++failures;
    }
}

inline int finish() {
    if (failures == 0) {
        std::cout << "All expectations passed\n";
    }
    return failures == 0 ? 0 : 1;
}

}

#define CONTRACT_EXPECT(expression) \
    ::contract::test::expect((expression), #expression, __FILE__, __LINE__)
#define CONTRACT_EXPECT_EQ(actual, expected) \
    ::contract::test::expect_equal((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
