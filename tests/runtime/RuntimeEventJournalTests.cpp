#include "TestSupport.hpp"

#include <contract/runtime/RuntimeEventJournal.hpp>

#include <contract/mission/Mission.hpp>
#include <contract/runtime/RuntimeCommand.hpp>
#include <contract/scene/Scene.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

int main() {
    using namespace contract;

    runtime::RuntimeEventJournal journal(3);
    const std::vector<runtime::RuntimeEvent> first_batch{
        runtime::EntityEnabledChangedEvent{
            scene::EntityId("entity.synthetic"),
            false},
        runtime::ObjectiveProgressChangedEvent{
            mission::ObjectiveId("objective.synthetic"),
            runtime::ObjectiveProgress::completed}};

    const auto first = journal.append(first_batch);
    CONTRACT_EXPECT(first.has_value());
    CONTRACT_EXPECT_EQ(first.value().first_sequence, std::uint64_t{0});
    CONTRACT_EXPECT_EQ(first.value().event_count, std::size_t{2});
    CONTRACT_EXPECT_EQ(journal.events().size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(journal.events()[0].sequence, std::uint64_t{0});
    CONTRACT_EXPECT_EQ(journal.events()[1].sequence, std::uint64_t{1});
    CONTRACT_EXPECT_EQ(journal.next_sequence(), std::uint64_t{2});

    const std::vector<runtime::RuntimeEvent> too_many{
        runtime::EntityEnabledChangedEvent{
            scene::EntityId("entity.second"),
            true},
        runtime::EntityEnabledChangedEvent{
            scene::EntityId("entity.third"),
            true}};
    const auto rejected = journal.append(too_many);
    CONTRACT_EXPECT(!rejected.has_value());
    CONTRACT_EXPECT_EQ(
        rejected.error().code,
        runtime::RuntimeEventJournalErrorCode::capacity_exceeded);
    CONTRACT_EXPECT_EQ(journal.events().size(), std::size_t{2});
    CONTRACT_EXPECT_EQ(journal.next_sequence(), std::uint64_t{2});

    const auto drained = journal.drain();
    CONTRACT_EXPECT_EQ(drained.size(), std::size_t{2});
    CONTRACT_EXPECT(journal.events().empty());
    CONTRACT_EXPECT_EQ(journal.next_sequence(), std::uint64_t{2});

    const std::vector<runtime::RuntimeEvent> final_batch{
        runtime::EntityEnabledChangedEvent{
            scene::EntityId("entity.final"),
            true}};
    const auto final = journal.append(final_batch);
    CONTRACT_EXPECT(final.has_value());
    CONTRACT_EXPECT_EQ(final.value().first_sequence, std::uint64_t{2});
    CONTRACT_EXPECT_EQ(journal.events()[0].sequence, std::uint64_t{2});

    const std::vector<runtime::RuntimeEvent> empty;
    const auto empty_result = journal.append(empty);
    CONTRACT_EXPECT(empty_result.has_value());
    CONTRACT_EXPECT_EQ(
        empty_result.value().first_sequence,
        std::uint64_t{3});
    CONTRACT_EXPECT_EQ(empty_result.value().event_count, std::size_t{0});

    return test::finish();
}
