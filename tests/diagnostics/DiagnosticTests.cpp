#include "TestSupport.hpp"

#include <contract/diagnostics/DiagnosticSink.hpp>

#include <cstddef>
#include <filesystem>
#include <string>

int main() {
    using namespace contract::diagnostics;

    DiagnosticBuffer buffer;
    buffer.emit(
        {
            Severity::information,
            "synthetic.info",
            "Synthetic information",
            std::nullopt,
            std::nullopt
        });
    buffer.emit(
        {
            Severity::error,
            "synthetic.error",
            "Synthetic error",
            std::filesystem::path("synthetic/input.bin"),
            std::size_t{17}
        });

    CONTRACT_EXPECT_EQ(buffer.diagnostics().size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(
        buffer.diagnostics()[0].code,
        std::string("synthetic.info"));
    CONTRACT_EXPECT_EQ(
        buffer.diagnostics()[1].path.value(),
        std::filesystem::path("synthetic/input.bin"));
    CONTRACT_EXPECT_EQ(
        buffer.diagnostics()[1].byte_offset.value(),
        std::size_t{17});
    CONTRACT_EXPECT(buffer.has_at_least(Severity::warning));
    CONTRACT_EXPECT(buffer.has_at_least(Severity::error));
    CONTRACT_EXPECT(!buffer.has_at_least(Severity::fatal));
    CONTRACT_EXPECT_EQ(severity_name(Severity::trace), std::string_view("trace"));
    CONTRACT_EXPECT_EQ(severity_name(Severity::fatal), std::string_view("fatal"));

    buffer.clear();
    CONTRACT_EXPECT(buffer.diagnostics().empty());
    CONTRACT_EXPECT(!buffer.has_at_least(Severity::trace));

    return contract::test::finish();
}
