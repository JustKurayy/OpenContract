#include "TestSupport.hpp"

#include <contract/datasource/DataSource.hpp>
#include <contract/formats/ZipArchive.hpp>

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

struct SyntheticEntry {
    std::string name;
    std::vector<std::byte> contents;
    contract::formats::ZipCompressionMethod method{
        contract::formats::ZipCompressionMethod::stored};
};

void append_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void append_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        bytes.push_back(
            static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

void append_string(std::vector<std::byte>& bytes, const std::string& value) {
    for (const auto character : value) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
}

std::vector<std::byte> deflate_raw(std::span<const std::byte> contents) {
    z_stream stream{};
    CONTRACT_EXPECT_EQ(deflateInit2(
        &stream,
        Z_DEFAULT_COMPRESSION,
        Z_DEFLATED,
        -MAX_WBITS,
        8,
        Z_DEFAULT_STRATEGY), Z_OK);

    std::vector<std::byte> compressed(
        static_cast<std::size_t>(compressBound(
            static_cast<uLong>(contents.size()))));
    stream.next_in = reinterpret_cast<Bytef*>(
        const_cast<std::byte*>(contents.data()));
    stream.avail_in = static_cast<uInt>(contents.size());
    stream.next_out = reinterpret_cast<Bytef*>(compressed.data());
    stream.avail_out = static_cast<uInt>(compressed.size());
    CONTRACT_EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
    CONTRACT_EXPECT_EQ(deflateEnd(&stream), Z_OK);
    compressed.resize(static_cast<std::size_t>(stream.total_out));
    return compressed;
}

std::vector<std::byte> make_archive(
    const std::vector<SyntheticEntry>& entries,
    bool encrypt_first = false) {
    struct CentralEntry {
        const SyntheticEntry* source;
        std::vector<std::byte> compressed;
        std::uint32_t crc;
        std::uint32_t local_offset;
    };

    std::vector<std::byte> bytes;
    std::vector<CentralEntry> central_entries;
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        auto compressed = entry.method ==
                                  contract::formats::ZipCompressionMethod::deflate
            ? deflate_raw(entry.contents)
            : entry.contents;
        const auto crc = static_cast<std::uint32_t>(crc32(
            0,
            reinterpret_cast<const Bytef*>(entry.contents.data()),
            static_cast<uInt>(entry.contents.size())));
        const auto local_offset = static_cast<std::uint32_t>(bytes.size());

        append_u32(bytes, 0x04034b50U);
        append_u16(bytes, 20);
        append_u16(bytes, encrypt_first && index == 0 ? 1 : 0);
        append_u16(bytes, static_cast<std::uint16_t>(entry.method));
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u32(bytes, crc);
        append_u32(bytes, static_cast<std::uint32_t>(compressed.size()));
        append_u32(bytes, static_cast<std::uint32_t>(entry.contents.size()));
        append_u16(bytes, static_cast<std::uint16_t>(entry.name.size()));
        append_u16(bytes, 0);
        append_string(bytes, entry.name);
        bytes.insert(bytes.end(), compressed.begin(), compressed.end());

        central_entries.push_back(
            {&entry, std::move(compressed), crc, local_offset});
    }

    const auto central_offset = static_cast<std::uint32_t>(bytes.size());
    for (std::size_t index = 0; index < central_entries.size(); ++index) {
        const auto& entry = central_entries[index];
        append_u32(bytes, 0x02014b50U);
        append_u16(bytes, 20);
        append_u16(bytes, 20);
        append_u16(bytes, encrypt_first && index == 0 ? 1 : 0);
        append_u16(bytes, static_cast<std::uint16_t>(entry.source->method));
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u32(bytes, entry.crc);
        append_u32(bytes, static_cast<std::uint32_t>(entry.compressed.size()));
        append_u32(
            bytes,
            static_cast<std::uint32_t>(entry.source->contents.size()));
        append_u16(
            bytes,
            static_cast<std::uint16_t>(entry.source->name.size()));
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u16(bytes, 0);
        append_u32(bytes, 0);
        append_u32(bytes, entry.local_offset);
        append_string(bytes, entry.source->name);
    }

    const auto central_size =
        static_cast<std::uint32_t>(bytes.size()) - central_offset;
    append_u32(bytes, 0x06054b50U);
    append_u16(bytes, 0);
    append_u16(bytes, 0);
    append_u16(bytes, static_cast<std::uint16_t>(entries.size()));
    append_u16(bytes, static_cast<std::uint16_t>(entries.size()));
    append_u32(bytes, central_size);
    append_u32(bytes, central_offset);
    append_u16(bytes, 0);
    return bytes;
}

std::vector<std::byte> bytes(std::string_view value) {
    std::vector<std::byte> result;
    for (const auto character : value) {
        result.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(character)));
    }
    return result;
}

}

int main() {
    using contract::formats::ZipArchiveErrorCode;
    using contract::formats::ZipArchiveIndex;
    using contract::formats::ZipCompressionMethod;

    const auto archive_bytes = make_archive(
        {
            {"synthetic/first.bin", bytes("stored data")},
            {
                "synthetic/second.bin",
                bytes("deflated synthetic data"),
                ZipCompressionMethod::deflate
            }
        });
    contract::datasource::MemoryDataSource source(archive_bytes);
    contract::datasource::ReadBudget budget(archive_bytes.size() * 3U, 17);
    const auto archive = ZipArchiveIndex::read(source, budget);
    CONTRACT_EXPECT(archive.has_value());
    if (!archive.has_value()) {
        std::cerr << "archive error: " << archive.error().message
                  << " at " << archive.error().offset << '\n';
    }
    if (archive.has_value()) {
        CONTRACT_EXPECT_EQ(archive.value().entries().size(), std::size_t{2});
        CONTRACT_EXPECT_EQ(
            archive.value().entries()[0].name,
            std::string("synthetic/first.bin"));

        const auto* first = archive.value().find("synthetic/first.bin");
        const auto* second = archive.value().find("synthetic/second.bin");
        CONTRACT_EXPECT(first != nullptr);
        CONTRACT_EXPECT(second != nullptr);
        CONTRACT_EXPECT(
            archive.value().find("synthetic/missing.bin") == nullptr);
        if (first != nullptr) {
            const auto contents =
                archive.value().read_entry(source, *first, budget, 1024);
            CONTRACT_EXPECT(contents.has_value());
            if (contents.has_value()) {
                CONTRACT_EXPECT_EQ(contents.value(), bytes("stored data"));
            }
        }
        if (second != nullptr) {
            const auto contents =
                archive.value().read_entry(source, *second, budget, 1024);
            CONTRACT_EXPECT(contents.has_value());
            if (contents.has_value()) {
                CONTRACT_EXPECT_EQ(
                    contents.value(),
                    bytes("deflated synthetic data"));
            }
            contract::datasource::ReadBudget small_budget(1024, 64);
            const auto too_large =
                archive.value().read_entry(source, *second, small_budget, 4);
            CONTRACT_EXPECT(!too_large.has_value());
            if (!too_large.has_value()) {
                CONTRACT_EXPECT_EQ(
                    too_large.error().code,
                    ZipArchiveErrorCode::size_limit_exceeded);
            }
        }
    }

    auto trailer_bytes = archive_bytes;
    trailer_bytes.insert(trailer_bytes.end(), 22, std::byte{0x5a});
    contract::datasource::MemoryDataSource trailer_source(trailer_bytes);
    contract::datasource::ReadBudget trailer_budget(
        trailer_bytes.size() * 2U,
        64);
    const auto trailer_archive =
        ZipArchiveIndex::read(trailer_source, trailer_budget);
    CONTRACT_EXPECT(trailer_archive.has_value());
    if (trailer_archive.has_value()) {
        CONTRACT_EXPECT_EQ(
            trailer_archive.value().entries().size(),
            std::size_t{2});
    }

    auto truncated_bytes = archive_bytes;
    truncated_bytes.pop_back();
    contract::datasource::MemoryDataSource truncated_source(truncated_bytes);
    contract::datasource::ReadBudget truncated_budget(
        truncated_bytes.size(),
        truncated_bytes.size());
    const auto truncated =
        ZipArchiveIndex::read(truncated_source, truncated_budget);
    CONTRACT_EXPECT(!truncated.has_value());
    if (!truncated.has_value()) {
        CONTRACT_EXPECT_EQ(
            truncated.error().code,
            ZipArchiveErrorCode::invalid_archive);
    }

    const auto encrypted_bytes = make_archive(
        {{"synthetic/encrypted.bin", bytes("not encrypted")}},
        true);
    contract::datasource::MemoryDataSource encrypted_source(encrypted_bytes);
    contract::datasource::ReadBudget encrypted_budget(
        encrypted_bytes.size() * 2U,
        encrypted_bytes.size());
    const auto encrypted =
        ZipArchiveIndex::read(encrypted_source, encrypted_budget);
    CONTRACT_EXPECT(!encrypted.has_value());
    if (!encrypted.has_value()) {
        std::cerr << "encrypted error: " << encrypted.error().message
                  << " at " << encrypted.error().offset << '\n';
        CONTRACT_EXPECT_EQ(
            encrypted.error().code,
            ZipArchiveErrorCode::unsupported_encryption);
    }

    auto zip64_bytes = make_archive(
        {{"synthetic/zip64.bin", bytes("synthetic")}});
    zip64_bytes[zip64_bytes.size() - 12U] = std::byte{0xff};
    zip64_bytes[zip64_bytes.size() - 11U] = std::byte{0xff};
    contract::datasource::MemoryDataSource zip64_source(zip64_bytes);
    contract::datasource::ReadBudget zip64_budget(
        zip64_bytes.size() * 2U,
        zip64_bytes.size());
    const auto zip64 = ZipArchiveIndex::read(zip64_source, zip64_budget);
    CONTRACT_EXPECT(!zip64.has_value());
    if (!zip64.has_value()) {
        std::cerr << "zip64 error: " << zip64.error().message
                  << " at " << zip64.error().offset << '\n';
        CONTRACT_EXPECT_EQ(
            zip64.error().code,
            ZipArchiveErrorCode::unsupported_zip64);
    }

    return contract::test::finish();
}
