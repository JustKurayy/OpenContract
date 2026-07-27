#pragma once

#include <string_view>

namespace contract::scripting {

class IScriptRuntime {
public:
    virtual ~IScriptRuntime() = default;
    [[nodiscard]] virtual bool supports_language(std::string_view language) const noexcept = 0;
};

}
