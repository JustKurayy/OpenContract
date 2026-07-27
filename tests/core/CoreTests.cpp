#include "TestSupport.hpp"

#include <contract/core/Error.hpp>
#include <contract/core/Identifier.hpp>
#include <contract/core/Result.hpp>

#include <string>

int main() {
    using contract::core::Identifier;
    using contract::core::Result;

    auto success = Result<int, std::string>::success(42);
    CONTRACT_EXPECT(success.has_value());
    CONTRACT_EXPECT_EQ(success.value(), 42);

    auto failure = Result<int, std::string>::failure("synthetic failure");
    CONTRACT_EXPECT(!failure.has_value());
    CONTRACT_EXPECT_EQ(failure.error(), std::string("synthetic failure"));

    Identifier<struct SyntheticTag> identifier("synthetic-id");
    CONTRACT_EXPECT_EQ(identifier.value(), std::string("synthetic-id"));
    CONTRACT_EXPECT(identifier.valid());

    Identifier<struct SyntheticTag> dotted("package.synthetic_2");
    CONTRACT_EXPECT(dotted.valid());
    CONTRACT_EXPECT(!Identifier<struct SyntheticTag>("").valid());
    CONTRACT_EXPECT(!Identifier<struct SyntheticTag>(".leading").valid());
    CONTRACT_EXPECT(!Identifier<struct SyntheticTag>("trailing.").valid());
    CONTRACT_EXPECT(!Identifier<struct SyntheticTag>("contains space").valid());
    CONTRACT_EXPECT(!Identifier<struct SyntheticTag>("contains/slash").valid());
    CONTRACT_EXPECT(!Identifier<struct SyntheticTag>("contains\\slash").valid());
    CONTRACT_EXPECT(!Identifier<struct SyntheticTag>(
        std::string(129, 'a')).valid());
    CONTRACT_EXPECT(Identifier<struct SyntheticTag>(
        std::string(128, 'a')).valid());

    const contract::core::Error error{
        contract::core::ErrorCategory::invalid_input,
        "core.synthetic",
        "Synthetic error"};
    CONTRACT_EXPECT_EQ(error.category, contract::core::ErrorCategory::invalid_input);
    CONTRACT_EXPECT_EQ(error.code, std::string("core.synthetic"));

    return contract::test::finish();
}
