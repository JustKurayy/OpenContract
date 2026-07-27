#include "TestSupport.hpp"

#include <contract/runtime/RuntimeSystem.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

namespace {

contract::runtime::RuntimeWorld make_world() {
    using namespace contract;

    const scene::EntityDefinition entity{
        scene::EntityId("entity.synthetic"),
        {},
        {}};
    const mission::MissionObjective objective{
        mission::ObjectiveId("objective.synthetic"),
        {entity.id},
        {}};
    return runtime::RuntimeWorld(
        mission::MissionId("mission.synthetic"),
        scene::MapId("map.synthetic"),
        std::nullopt,
        {{entity, true}},
        {{objective, runtime::ObjectiveProgress::pending}});
}

class EnablementSystem final : public contract::runtime::RuntimeSystem {
public:
    explicit EnablementSystem(bool enabled)
        : enabled_(enabled) {}

    [[nodiscard]] contract::core::Result<
        void,
        contract::runtime::RuntimeSystemFailure>
    evaluate(
        const contract::runtime::RuntimeWorld& world,
        const contract::runtime::RuntimeSystemContext& context,
        contract::runtime::RuntimeCommandEmitter& emitter) const override {
        observed_enabled_ = world.entities()[0].enabled;
        observed_tick_ = context.tick;
        return emitter.emit(
            contract::runtime::SetEntityEnabledCommand{
                contract::scene::EntityId("entity.synthetic"),
                enabled_});
    }

    [[nodiscard]] bool observed_enabled() const noexcept {
        return observed_enabled_;
    }

    [[nodiscard]] std::uint64_t observed_tick() const noexcept {
        return observed_tick_;
    }

private:
    bool enabled_{true};
    mutable bool observed_enabled_{false};
    mutable std::uint64_t observed_tick_{0};
};

class FailingSystem final : public contract::runtime::RuntimeSystem {
public:
    [[nodiscard]] contract::core::Result<
        void,
        contract::runtime::RuntimeSystemFailure>
    evaluate(
        const contract::runtime::RuntimeWorld&,
        const contract::runtime::RuntimeSystemContext&,
        contract::runtime::RuntimeCommandEmitter&) const override {
        return contract::core::Result<
            void,
            contract::runtime::RuntimeSystemFailure>::failure(
            {
                contract::runtime::RuntimeSystemFailureCode::evaluation_failed,
                "Synthetic system failure"
            });
    }
};

}

int main() {
    using namespace std::chrono_literals;
    using namespace contract;

    const EnablementSystem disable_system(false);
    const EnablementSystem enable_system(true);
    const runtime::RuntimeSystem* ordered_systems[]{
        &disable_system,
        &enable_system};

    runtime::RuntimeSystemCoordinator coordinator;
    const auto ordered = coordinator.evaluate(
        make_world(),
        runtime::RuntimeSystemContext{17, 10ms},
        ordered_systems,
        std::size_t{2});
    CONTRACT_EXPECT(ordered.has_value());
    CONTRACT_EXPECT_EQ(ordered.value().size(), std::size_t{2});
    CONTRACT_EXPECT(
        std::holds_alternative<runtime::SetEntityEnabledCommand>(
            ordered.value()[0]));
    CONTRACT_EXPECT(
        !std::get<runtime::SetEntityEnabledCommand>(
             ordered.value()[0]).enabled);
    CONTRACT_EXPECT(
        std::get<runtime::SetEntityEnabledCommand>(
            ordered.value()[1]).enabled);
    CONTRACT_EXPECT(disable_system.observed_enabled());
    CONTRACT_EXPECT(enable_system.observed_enabled());
    CONTRACT_EXPECT_EQ(disable_system.observed_tick(), std::uint64_t{17});
    CONTRACT_EXPECT_EQ(enable_system.observed_tick(), std::uint64_t{17});

    const auto over_limit = coordinator.evaluate(
        make_world(),
        runtime::RuntimeSystemContext{0, 10ms},
        ordered_systems,
        std::size_t{1});
    CONTRACT_EXPECT(!over_limit.has_value());
    CONTRACT_EXPECT_EQ(
        over_limit.error().code,
        runtime::RuntimeSystemErrorCode::command_limit_exceeded);
    CONTRACT_EXPECT_EQ(over_limit.error().system_index, std::size_t{1});
    CONTRACT_EXPECT(over_limit.error().failure.has_value());
    CONTRACT_EXPECT_EQ(
        over_limit.error().failure->code,
        runtime::RuntimeSystemFailureCode::command_limit_exceeded);

    const FailingSystem failing_system;
    const runtime::RuntimeSystem* failing_systems[]{
        &disable_system,
        &failing_system};
    const auto failed = coordinator.evaluate(
        make_world(),
        runtime::RuntimeSystemContext{0, 10ms},
        failing_systems,
        std::size_t{2});
    CONTRACT_EXPECT(!failed.has_value());
    CONTRACT_EXPECT_EQ(
        failed.error().code,
        runtime::RuntimeSystemErrorCode::system_failed);
    CONTRACT_EXPECT_EQ(failed.error().system_index, std::size_t{1});
    CONTRACT_EXPECT(failed.error().failure.has_value());
    CONTRACT_EXPECT_EQ(
        failed.error().failure->code,
        runtime::RuntimeSystemFailureCode::evaluation_failed);

    const runtime::RuntimeSystem* null_systems[]{nullptr};
    const auto null_system = coordinator.evaluate(
        make_world(),
        runtime::RuntimeSystemContext{0, 10ms},
        null_systems,
        std::size_t{1});
    CONTRACT_EXPECT(!null_system.has_value());
    CONTRACT_EXPECT_EQ(
        null_system.error().code,
        runtime::RuntimeSystemErrorCode::null_system);
    CONTRACT_EXPECT_EQ(null_system.error().system_index, std::size_t{0});

    return test::finish();
}
