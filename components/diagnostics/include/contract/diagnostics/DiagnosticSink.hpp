#pragma once

#include <contract/diagnostics/Diagnostic.hpp>

#include <span>
#include <string_view>
#include <vector>

namespace contract::diagnostics {

class IDiagnosticSink {
public:
    virtual ~IDiagnosticSink() = default;
    virtual void emit(Diagnostic diagnostic) = 0;
};

class DiagnosticBuffer final : public IDiagnosticSink {
public:
    void emit(Diagnostic diagnostic) override;

    [[nodiscard]] std::span<const Diagnostic> diagnostics() const noexcept;
    [[nodiscard]] bool has_at_least(Severity severity) const noexcept;
    void clear() noexcept;

private:
    std::vector<Diagnostic> diagnostics_;
};

[[nodiscard]] std::string_view severity_name(Severity severity) noexcept;

}
