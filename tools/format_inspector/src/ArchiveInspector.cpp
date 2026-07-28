#include <contract/tools/ArchiveInspector.hpp>

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/PrimitiveContainer.hpp>
#include <contract/formats/PrimitiveSceneDecoder.hpp>
#include <contract/formats/ZipArchive.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
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

struct PrimitiveSummary {
    std::uint64_t directory_offset{0};
    std::uint32_t record_count{0};
    std::uint64_t largest_record_size{0};
    std::size_t mesh_count{0};
    std::size_t vertex_count{0};
    std::size_t index_count{0};
    std::size_t candidate_models{0};
    std::size_t rejected_models{0};
    std::size_t rejected_objects{0};
    std::map<std::uint32_t, std::size_t> kind_counts;
};

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
        } else if (argument == "--primitive-entry") {
            if (index + 1 >= arguments.size()) {
                errors << "--primitive-entry requires an archive entry name\n";
                return std::nullopt;
            }
            options.primitive_entry = arguments[++index];
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

    std::optional<PrimitiveSummary> primitive_summary;
    if (options.primitive_entry.has_value()) {
        const auto* entry = index.value().find(*options.primitive_entry);
        if (entry == nullptr) {
            errors << "Primitive entry was not found in the archive\n";
            return static_cast<int>(ArchiveInspectorExitCode::invalid_archive);
        }
        auto bytes = index.value().read_entry(
            source.value(),
            *entry,
            budget,
            256U * 1024U * 1024U);
        if (!bytes.has_value()) {
            errors << bytes.error().message
                   << " at byte "
                   << bytes.error().offset
                   << '\n';
            return static_cast<int>(ArchiveInspectorExitCode::invalid_archive);
        }

        datasource::MemoryDataSource primitive_source(bytes.value());
        datasource::ReadBudget primitive_budget(
            256U * 1024U * 1024U,
            1024U * 1024U);
        auto primitive = formats::PrimitiveContainerIndex::read(
            primitive_source,
            primitive_budget);
        if (!primitive.has_value()) {
            errors << primitive.error().message
                   << " at entry byte "
                   << primitive.error().offset
                   << '\n';
            return static_cast<int>(ArchiveInspectorExitCode::invalid_archive);
        }

        PrimitiveSummary summary;
        summary.directory_offset =
            primitive.value().header().directory_offset;
        summary.record_count = primitive.value().header().record_count;
        for (const auto& record : primitive.value().records()) {
            summary.largest_record_size =
                std::max(summary.largest_record_size, record.size);
            ++summary.kind_counts[record.kind];
        }
        auto scene = formats::PrimitiveSceneDecoder::decode(
            primitive.value(),
            primitive_source,
            primitive_budget);
        if (!scene.has_value()) {
            errors << scene.error().message
                   << " at entry byte "
                   << scene.error().offset
                   << '\n';
            return static_cast<int>(ArchiveInspectorExitCode::invalid_archive);
        }
        summary.mesh_count = scene.value().meshes.size();
        summary.candidate_models = scene.value().candidate_models;
        summary.rejected_models = scene.value().rejected_models;
        summary.rejected_objects = scene.value().rejected_objects;
        for (const auto& mesh : scene.value().meshes) {
            summary.vertex_count += mesh.positions.size();
            summary.index_count += mesh.indices.size();
        }
        primitive_summary = std::move(summary);
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
        output << ']';
        if (primitive_summary.has_value()) {
            output << ",\"primitive_container\":{\"entry\":\""
                   << json_escape(*options.primitive_entry)
                   << "\",\"directory_offset\":"
                   << primitive_summary->directory_offset
                   << ",\"record_count\":"
                   << primitive_summary->record_count
                   << ",\"largest_record_size\":"
                   << primitive_summary->largest_record_size
                   << ",\"mesh_count\":"
                   << primitive_summary->mesh_count
                   << ",\"vertex_count\":"
                   << primitive_summary->vertex_count
                   << ",\"index_count\":"
                   << primitive_summary->index_count
                   << ",\"candidate_models\":"
                   << primitive_summary->candidate_models
                   << ",\"rejected_models\":"
                   << primitive_summary->rejected_models
                   << ",\"rejected_objects\":"
                   << primitive_summary->rejected_objects
                   << ",\"kind_counts\":{";
            std::size_t kind_index = 0;
            for (const auto& [kind, count] :
                 primitive_summary->kind_counts) {
                if (kind_index++ != 0) {
                    output << ',';
                }
                output << '"' << kind << "\":" << count;
            }
            output << "}}";
        }
        output << "}\n";
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
        if (primitive_summary.has_value()) {
            output << "Primitive entry: "
                   << *options.primitive_entry
                   << '\n'
                   << "Directory offset: "
                   << primitive_summary->directory_offset
                   << '\n'
                   << "Records: "
                   << primitive_summary->record_count
                   << '\n'
                   << "Largest record: "
                   << primitive_summary->largest_record_size
                   << '\n'
                   << "Decoded meshes: "
                   << primitive_summary->mesh_count
                   << '\n'
                   << "Decoded vertices: "
                   << primitive_summary->vertex_count
                   << '\n'
                   << "Decoded indices: "
                   << primitive_summary->index_count
                   << '\n';
        }
    }
    return static_cast<int>(ArchiveInspectorExitCode::success);
}

void print_archive_inspector_help(std::ostream& output) {
    output
        << "Usage: contract-archive-inspect --archive <file>"
        << " [--primitive-entry <name>] [--json] [--help]\n"
        << "Lists archive metadata without extracting entry contents.\n";
}

}
