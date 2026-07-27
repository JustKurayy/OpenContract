#include <contract/runtime/RuntimeEventJournal.hpp>

#include <limits>
#include <utility>

namespace contract::runtime {

RuntimeEventJournal::RuntimeEventJournal(std::size_t capacity)
    : capacity_(capacity) {}

core::Result<RuntimeEventAppendResult, RuntimeEventJournalError>
RuntimeEventJournal::append(std::span<const RuntimeEvent> events) {
    if (events.size() > capacity_ - events_.size()) {
        return core::Result<
            RuntimeEventAppendResult,
            RuntimeEventJournalError>::failure(
            {
                RuntimeEventJournalErrorCode::capacity_exceeded,
                "Runtime event journal capacity would be exceeded"
            });
    }

    const auto event_count = static_cast<std::uint64_t>(events.size());
    if (event_count >
        std::numeric_limits<std::uint64_t>::max() - next_sequence_) {
        return core::Result<
            RuntimeEventAppendResult,
            RuntimeEventJournalError>::failure(
            {
                RuntimeEventJournalErrorCode::sequence_overflow,
                "Runtime event sequence would overflow"
            });
    }

    const auto first_sequence = next_sequence_;
    events_.reserve(events_.size() + events.size());
    for (const auto& event : events) {
        events_.push_back({next_sequence_, event});
        ++next_sequence_;
    }
    return core::Result<
        RuntimeEventAppendResult,
        RuntimeEventJournalError>::success(
        {first_sequence, events.size()});
}

std::span<const SequencedRuntimeEvent>
RuntimeEventJournal::events() const noexcept {
    return events_;
}

std::vector<SequencedRuntimeEvent> RuntimeEventJournal::drain() {
    std::vector<SequencedRuntimeEvent> drained;
    drained.swap(events_);
    return drained;
}

std::size_t RuntimeEventJournal::capacity() const noexcept {
    return capacity_;
}

std::uint64_t RuntimeEventJournal::next_sequence() const noexcept {
    return next_sequence_;
}

}
