#pragma once

#include <string>

namespace contract::core {

enum class ErrorCategory {
    invalid_input,
    not_found,
    permission_denied,
    unsupported,
    malformed_data,
    io_failure,
    internal
};

struct Error {
    ErrorCategory category{ErrorCategory::internal};
    std::string code;
    std::string message;
};

}
