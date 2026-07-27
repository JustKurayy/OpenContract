#include <contract/diagnostics/DiagnosticSink.hpp>

#include <algorithm>
#include <utility>

namespace contract::diagnostics {

void DiagnosticBuffer::emit(Diagnostic diagnostic) {
    diagnostics_.push_back(std::move(diagnostic));
}

std::span<const Diagnostic> DiagnosticBuffer::diagnostics() const noexcept {
    return diagnostics_;
}

bool DiagnosticBuffer::has_at_least(Severity severity) const noexcept {
    return std::ranges::any_of(
        diagnostics_,
        [severity](const Diagnostic& diagnostic) {
            return static_cast<int>(diagnostic.severity) >=
                   static_cast<int>(severity);
        });
}

void DiagnosticBuffer::clear() noexcept {
    diagnostics_.clear();
}

std::string_view severity_name(Severity severity) noexcept {
    switch (severity) {
    case Severity::trace:
        return "trace";
    case Severity::information:
        return "information";
    case Severity::warning:
        return "warning";
    case Severity::error:
        return "error";
    case Severity::fatal:
        return "fatal";
    }
    return "unknown";
}

}
