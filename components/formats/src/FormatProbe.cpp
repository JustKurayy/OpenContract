#include <contract/formats/FormatProbe.hpp>

#include <utility>

namespace contract::formats {

void FormatProbeDispatcher::add(std::unique_ptr<IFormatProbe> probe) {
    if (probe != nullptr) {
        probes_.push_back(std::move(probe));
    }
}

FormatProbeResult FormatProbeDispatcher::probe(const FormatProbeInput& input) const {
    FormatProbeResult best{"unsupported", FormatConfidence::none};
    for (const auto& candidate : probes_) {
        auto result = candidate->probe(input);
        if (result.confidence > best.confidence) {
            best = std::move(result);
        }
    }
    return best;
}

}
