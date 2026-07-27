#pragma once

#include <contract/core/Result.hpp>
#include <contract/runtime/RuntimeCommand.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace contract::runtime {

enum class RuntimeEventJournalErrorCode {
    capacity_exceeded,
    sequence_overflow
};

struct RuntimeEventJournalError {
    RuntimeEventJournalErrorCode code{
        RuntimeEventJournalErrorCode::capacity_exceeded};
    std::string message;
};

struct RuntimeEventAppendResult {
    std::uint64_t first_sequence{0};
    std::size_t event_count{0};
};

struct SequencedRuntimeEvent {
    std::uint64_t sequence{0};
    RuntimeEvent event;
};

class RuntimeEventJournal {
public:
    explicit RuntimeEventJournal(std::size_t capacity);

    [[nodiscard]] core::Result<
        RuntimeEventAppendResult,
        RuntimeEventJournalError>
    append(std::span<const RuntimeEvent> events);

    [[nodiscard]] std::span<const SequencedRuntimeEvent> events() const noexcept;
    [[nodiscard]] std::vector<SequencedRuntimeEvent> drain();
    [[nodiscard]] std::size_t capacity() const noexcept;
    [[nodiscard]] std::uint64_t next_sequence() const noexcept;

private:
    std::size_t capacity_{0};
    std::uint64_t next_sequence_{0};
    std::vector<SequencedRuntimeEvent> events_;
};

}
