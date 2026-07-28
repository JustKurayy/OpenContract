#include <contract/tools/ArchiveInspector.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/ZipArchive.hpp>

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string_view>

namespace contract::tools {
namespace {

std::string json_escape(std::string_view value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') {
            output << '\\' << static_cast<char>(character);
        } else if (character < 0x20U) {
            output << "\\u"
                   << std::hex
                   << std::setw(4)
                   << std::setfill('0')
                   << static_cast<unsigned int>(character)
                   << std::dec;
        } else {
            output << static_cast<char>(character);
        }
    }
    return output.str();
}

std::string compression_name(formats::ZipCompressionMethod method) {
    switch (method) {
    case formats::ZipCompressionMethod::stored:
        return "stored";
    case formats::ZipCompressionMethod::deflate:
        return "deflate";
    }
    return "unsupported";
}

}

std::optional<ArchiveInspectorOptions> parse_archive_inspector_options(
    const std::vector<std::string>& arguments,
    std::ostream& errors) {
    ArchiveInspectorOptions options;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--archive") {
            if (index + 1 >= arguments.size()) {
                errors << "--archive requires a path\n";
                return std::nullopt;
            }
            options.archive_path = std::filesystem::path(arguments[++index]);
        } else {
            errors << "Unknown option: " << argument << '\n';
            return std::nullopt;
        }
    }
    if (!options.help && !options.archive_path.has_value()) {
        errors << "--archive is required\n";
        return std::nullopt;
    }
    return options;
}

int run_archive_inspector(
    const ArchiveInspectorOptions& options,
    std::ostream& output,
    std::ostream& errors) {
    if (!options.archive_path.has_value()) {
        errors << "No archive path was supplied\n";
        return static_cast<int>(ArchiveInspectorExitCode::usage_error);
    }

    auto source = datasource::FileDataSource::open(*options.archive_path);
    if (!source.has_value()) {
        errors << source.error().message << '\n';
        return static_cast<int>(ArchiveInspectorExitCode::source_error);
    }
    datasource::ReadBudget budget(32U * 1024U * 1024U, 1024U * 1024U);
    auto index = formats::ZipArchiveIndex::read(source.value(), budget);
    if (!index.has_value()) {
        errors << index.error().message
               << " at byte "
               << index.error().offset
               << '\n';
        return static_cast<int>(ArchiveInspectorExitCode::invalid_archive);
    }

    if (options.json) {
        output << "{\"archive\":\""
               << json_escape(options.archive_path->string())
               << "\",\"entries\":[";
        for (std::size_t entry_index = 0;
             entry_index < index.value().entries().size();
             ++entry_index) {
            const auto& entry = index.value().entries()[entry_index];
            if (entry_index != 0) {
                output << ',';
            }
            output << "{\"name\":\""
                   << json_escape(entry.name)
                   << "\",\"compression\":\""
                   << compression_name(entry.compression)
                   << "\",\"compressed_size\":"
                   << entry.compressed_size
                   << ",\"uncompressed_size\":"
                   << entry.uncompressed_size
                   << '}';
        }
        output << "]}\n";
    } else {
        output << "Archive: " << options.archive_path->string() << '\n'
               << "Entries: " << index.value().entries().size() << '\n';
        for (const auto& entry : index.value().entries()) {
            output << entry.name
                   << '\t'
                   << compression_name(entry.compression)
                   << '\t'
                   << entry.compressed_size
                   << '\t'
                   << entry.uncompressed_size
                   << '\n';
        }
    }
    return static_cast<int>(ArchiveInspectorExitCode::success);
}

void print_archive_inspector_help(std::ostream& output) {
    output
        << "Usage: contract-archive-inspect --archive <file> [--json] [--help]\n"
        << "Lists archive metadata without extracting entry contents.\n";
}

}
