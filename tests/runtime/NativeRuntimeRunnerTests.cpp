#include "TestSupport.hpp"

#include <contract/platform/NativeRuntimeRunner.hpp>
#include <contract/runtime/RuntimeHost.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace {

contract::runtime::RuntimeHost make_host() {
    using namespace std::chrono_literals;
    using namespace contract;

    const scene::EntityDefinition entity{
        scene::EntityId("entity.synthetic"),
        {},
        {}};
    const mission::MissionObjective objective{
        mission::ObjectiveId("objective.synthetic"),
        {entity.id},
        {}};
    runtime::RuntimeWorld world(
        mission::MissionId("mission.synthetic"),
        scene::MapId("map.synthetic"),
        std::nullopt,
        {{entity, true}},
        {{objective, runtime::ObjectiveProgress::pending}});
    auto session = runtime::RuntimeSession::create(
        std::move(world),
        1ms,
        std::size_t{4},
        std::size_t{4});
    auto host = runtime::RuntimeHost::create(
        std::move(session.value()),
        {});
    return std::move(host.value());
}

}

int main() {
    using namespace contract;

    auto host = make_host();
    platform::NativeRuntimeRunner runner(
        platform::NativeWindowVisibility::hidden);
    const auto result = runner.run(
        host,
        runtime::RuntimeRunnerOptions{std::uint64_t{2}});
    CONTRACT_EXPECT(result.has_value());
    CONTRACT_EXPECT(
        host.observe().completed_ticks >= std::uint64_t{2});

    auto invalid_host = make_host();
    const auto invalid = runner.run(
        invalid_host,
        runtime::RuntimeRunnerOptions{std::uint64_t{0}});
    CONTRACT_EXPECT(!invalid.has_value());
    CONTRACT_EXPECT_EQ(
        invalid.error().code,
        runtime::RuntimeRunnerErrorCode::platform_failed);
    CONTRACT_EXPECT_EQ(
        invalid_host.observe().completed_ticks,
        std::uint64_t{0});

    return test::finish();
}
