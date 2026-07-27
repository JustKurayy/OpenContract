#include "TestSupport.hpp"

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/FormatProbe.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace {

class SyntheticProbe final : public contract::formats::IFormatProbe {
public:
    SyntheticProbe(std::string name, contract::formats::FormatConfidence confidence)
        : name_(std::move(name)), confidence_(confidence) {}

    contract::formats::FormatProbeResult probe(
        const contract::formats::FormatProbeInput&) const override {
        return {name_, confidence_};
    }

private:
    std::string name_;
    contract::formats::FormatConfidence confidence_;
};

class ReadingSyntheticProbe final : public contract::formats::IFormatProbe {
public:
    contract::formats::FormatProbeResult probe(
        const contract::formats::FormatProbeInput& input) const override {
        const auto prefix = input.source.read(0, 2, input.budget);
        if (!prefix.has_value() || prefix.value().size() != 2 ||
            prefix.value()[0] != std::byte{0x42} ||
            prefix.value()[1] != std::byte{0x24}) {
            return {"unsupported", contract::formats::FormatConfidence::none};
        }
        return {"synthetic-signature", contract::formats::FormatConfidence::high};
    }
};

}

int main() {
    using contract::formats::FormatConfidence;

    const std::array bytes{
        std::byte{0x42},
        std::byte{0x24},
        std::byte{0x10}};
    contract::datasource::MemoryDataSource source(bytes);
    contract::datasource::ReadBudget budget(2, 2);

    contract::formats::FormatProbeDispatcher dispatcher;
    dispatcher.add(std::make_unique<SyntheticProbe>("weak-synthetic", FormatConfidence::low));
    dispatcher.add(std::make_unique<ReadingSyntheticProbe>());

    const contract::formats::FormatProbeInput input{"synthetic.asset", source, budget};
    const auto best = dispatcher.probe(input);
    CONTRACT_EXPECT(best.supported());
    CONTRACT_EXPECT_EQ(best.format_name, std::string("synthetic-signature"));
    CONTRACT_EXPECT_EQ(best.confidence, FormatConfidence::high);
    CONTRACT_EXPECT_EQ(budget.consumed(), std::uint64_t{2});

    contract::formats::FormatProbeDispatcher empty;
    contract::datasource::ReadBudget empty_budget(0, 0);
    const contract::formats::FormatProbeInput empty_input{
        "synthetic.asset",
        source,
        empty_budget};
    const auto unsupported = empty.probe(empty_input);
    CONTRACT_EXPECT(!unsupported.supported());
    CONTRACT_EXPECT_EQ(unsupported.confidence, FormatConfidence::none);

    return contract::test::finish();
}
