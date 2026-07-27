#pragma once

#include <compare>
#include <string>
#include <utility>

namespace contract::core {

template <typename Tag>
class Identifier {
public:
    Identifier() = default;

    explicit Identifier(std::string value)
        : value_(std::move(value)) {}

    [[nodiscard]] const std::string& value() const noexcept {
        return value_;
    }

    [[nodiscard]] bool valid() const noexcept {
        if (value_.empty() || value_.size() > 128 ||
            !is_ascii_alphanumeric(value_.front()) ||
            !is_ascii_alphanumeric(value_.back())) {
            return false;
        }
        for (const char character : value_) {
            if (!is_ascii_alphanumeric(character) &&
                character != '.' &&
                character != '-' &&
                character != '_') {
                return false;
            }
        }
        return true;
    }

    auto operator<=>(const Identifier&) const = default;

private:
    [[nodiscard]] static bool is_ascii_alphanumeric(char character) noexcept {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9');
    }

    std::string value_;
};

}
