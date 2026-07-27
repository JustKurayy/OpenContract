#pragma once

#include <contract/datasource/DataSource.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace contract::formats {

enum class FormatConfidence {
    none,
    low,
    medium,
    high
};

struct FormatProbeInput {
    std::filesystem::path path;
    datasource::IReadOnlyDataSource& source;
    datasource::ReadBudget& budget;
};

struct FormatProbeResult {
    std::string format_name;
    FormatConfidence confidence{FormatConfidence::none};

    [[nodiscard]] bool supported() const noexcept {
        return confidence != FormatConfidence::none;
    }
};

class IFormatProbe {
public:
    virtual ~IFormatProbe() = default;
    [[nodiscard]] virtual FormatProbeResult probe(const FormatProbeInput& input) const = 0;
};

class FormatProbeDispatcher {
public:
    void add(std::unique_ptr<IFormatProbe> probe);
    [[nodiscard]] FormatProbeResult probe(const FormatProbeInput& input) const;

private:
    std::vector<std::unique_ptr<IFormatProbe>> probes_;
};

}
