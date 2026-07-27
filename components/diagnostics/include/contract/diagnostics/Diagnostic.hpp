#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace contract::diagnostics {

enum class Severity {
    trace,
    information,
    warning,
    error,
    fatal
};

struct Diagnostic {
    Severity severity{Severity::information};
    std::string code;
    std::string message;
    std::optional<std::filesystem::path> path;
    std::optional<std::size_t> byte_offset;
};

}
