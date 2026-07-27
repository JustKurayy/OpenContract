#pragma once

#include <utility>
#include <variant>

namespace contract::core {

template <typename T, typename E>
class Result {
public:
    static Result success(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }

    static Result failure(E error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return value_.index() == 0;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] T& value() {
        return std::get<0>(value_);
    }

    [[nodiscard]] const T& value() const {
        return std::get<0>(value_);
    }

    [[nodiscard]] E& error() {
        return std::get<1>(value_);
    }

    [[nodiscard]] const E& error() const {
        return std::get<1>(value_);
    }

private:
    template <std::size_t Index, typename Value>
    explicit Result(std::in_place_index_t<Index> index, Value&& value)
        : value_(index, std::forward<Value>(value)) {}

    std::variant<T, E> value_;
};

template <typename E>
class Result<void, E> {
public:
    static Result success() {
        return Result(std::in_place_index<0>);
    }

    static Result failure(E error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool has_value() const noexcept {
        return value_.index() == 0;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return has_value();
    }

    [[nodiscard]] E& error() {
        return std::get<1>(value_);
    }

    [[nodiscard]] const E& error() const {
        return std::get<1>(value_);
    }

private:
    explicit Result(std::in_place_index_t<0>)
        : value_(std::in_place_index<0>) {}

    explicit Result(std::in_place_index_t<1>, E error)
        : value_(std::in_place_index<1>, std::move(error)) {}

    std::variant<std::monostate, E> value_;
};

}
