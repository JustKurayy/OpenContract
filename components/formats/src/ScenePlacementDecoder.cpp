#include <contract/formats/ScenePlacementDecoder.hpp>

#include <contract/binaryio/BinaryReader.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace contract::formats {
namespace {

constexpr std::size_t kHeaderSize = 31;
constexpr std::string_view kMagic{"IOPacked v0.1"};

constexpr std::uint8_t kArray = 0x01;
constexpr std::uint8_t kBeginObject = 0x02;
constexpr std::uint8_t kReference = 0x03;
constexpr std::uint8_t kContainer = 0x04;
constexpr std::uint8_t kChar = 0x05;
constexpr std::uint8_t kBool = 0x06;
constexpr std::uint8_t kInt8 = 0x07;
constexpr std::uint8_t kInt16 = 0x08;
constexpr std::uint8_t kInt32 = 0x09;
constexpr std::uint8_t kFloat32 = 0x0a;
constexpr std::uint8_t kFloat64 = 0x0b;
constexpr std::uint8_t kString = 0x0c;
constexpr std::uint8_t kRawData = 0x0d;
constexpr std::uint8_t kEnum = 0x0e;
constexpr std::uint8_t kStringArray = 0x0f;
constexpr std::uint8_t kBitfield = 0x10;
constexpr std::uint8_t kEndArray = 0x7c;
constexpr std::uint8_t kSkipMark = 0x7d;
constexpr std::uint8_t kEndObject = 0x7e;
constexpr std::uint8_t kEndOfStream = 0x7f;
constexpr std::uint8_t kNamedMask = 0x80;

struct Instruction {
    std::uint8_t opcode{0};
    std::uint64_t offset{0};
    std::uint32_t word{0};
    float floating{0.0F};
};

core::Result<ScenePlacementDecodeResult, ScenePlacementError> failure(
    ScenePlacementErrorCode code,
    std::uint64_t offset,
    std::string message) {
    return core::Result<
        ScenePlacementDecodeResult,
        ScenePlacementError>::failure(
        {code, offset, std::move(message)});
}

ScenePlacementError truncated_error(
    const binaryio::BinaryError& error,
    std::string message) {
    return {
        ScenePlacementErrorCode::truncated,
        error.offset,
        std::move(message)
    };
}

std::uint8_t base_opcode(std::uint8_t opcode) {
    if (opcode == 0x8fU) {
        return kBitfield;
    }
    if (opcode >= kNamedMask) {
        return static_cast<std::uint8_t>(opcode - kNamedMask);
    }
    return opcode;
}

bool is_opcode(
    const Instruction& instruction,
    std::uint8_t expected) {
    return base_opcode(instruction.opcode) == expected;
}

core::Result<void, ScenePlacementError> expect_opcode(
    binaryio::BinaryReader& reader,
    std::uint8_t expected,
    std::string message) {
    const auto offset = reader.offset();
    const auto opcode = reader.read_u8();
    if (!opcode.has_value()) {
        return core::Result<void, ScenePlacementError>::failure(
            truncated_error(opcode.error(), std::move(message)));
    }
    if (base_opcode(opcode.value()) != expected) {
        return core::Result<void, ScenePlacementError>::failure(
            {
                ScenePlacementErrorCode::unsupported_layout,
                offset,
                std::move(message)
            });
    }
    return core::Result<void, ScenePlacementError>::success();
}

core::Result<void, ScenePlacementError> skip_word(
    binaryio::BinaryReader& reader,
    std::string message) {
    const auto value = reader.read_u32();
    if (!value.has_value()) {
        return core::Result<void, ScenePlacementError>::failure(
            truncated_error(value.error(), std::move(message)));
    }
    return core::Result<void, ScenePlacementError>::success();
}

core::Result<void, ScenePlacementError> skip_definitions(
    binaryio::BinaryReader& reader,
    const ScenePlacementDecodeLimits& limits) {
    auto root = expect_opcode(
        reader,
        kContainer,
        "Scene property definitions do not start with a container");
    if (!root.has_value()) {
        return root;
    }
    const auto count = reader.read_u32();
    if (!count.has_value()) {
        return core::Result<void, ScenePlacementError>::failure(
            truncated_error(
                count.error(),
                "Scene property definition count is truncated"));
    }
    if (count.value() > limits.max_definitions) {
        return core::Result<void, ScenePlacementError>::failure(
            {
                ScenePlacementErrorCode::limit_exceeded,
                reader.offset() - 4U,
                "Scene property definition count exceeds configured limits"
            });
    }

    for (std::uint32_t index = 0; index < count.value(); ++index) {
        auto name = expect_opcode(
            reader,
            kString,
            "Scene property definition name is invalid");
        if (!name.has_value()) {
            return name;
        }
        auto name_index = skip_word(
            reader,
            "Scene property definition name is truncated");
        if (!name_index.has_value()) {
            return name_index;
        }
        auto kind_opcode = expect_opcode(
            reader,
            kInt32,
            "Scene property definition kind is invalid");
        if (!kind_opcode.has_value()) {
            return kind_opcode;
        }
        const auto kind_offset = reader.offset();
        const auto kind = reader.read_u32();
        if (!kind.has_value()) {
            return core::Result<void, ScenePlacementError>::failure(
                truncated_error(
                    kind.error(),
                    "Scene property definition kind is truncated"));
        }

        if (kind.value() == 2U || kind.value() == 3U) {
            auto length_opcode = expect_opcode(
                reader,
                kInt32,
                "Scene property array length is invalid");
            if (!length_opcode.has_value()) {
                return length_opcode;
            }
            const auto length = reader.read_u32();
            if (!length.has_value()) {
                return core::Result<void, ScenePlacementError>::failure(
                    truncated_error(
                        length.error(),
                        "Scene property array length is truncated"));
            }
            auto array_opcode = expect_opcode(
                reader,
                kArray,
                "Scene property array declaration is invalid");
            if (!array_opcode.has_value()) {
                return array_opcode;
            }
            const auto repeated_length = reader.read_u32();
            if (!repeated_length.has_value()) {
                return core::Result<void, ScenePlacementError>::failure(
                    truncated_error(
                        repeated_length.error(),
                        "Scene property array declaration is truncated"));
            }
            if (repeated_length.value() != length.value() ||
                length.value() > limits.max_instructions) {
                return core::Result<void, ScenePlacementError>::failure(
                    {
                        ScenePlacementErrorCode::limit_exceeded,
                        reader.offset() - 4U,
                        "Scene property array length is inconsistent"
                    });
            }
            const auto element_opcode =
                kind.value() == 2U ? kInt32 : kFloat32;
            for (std::uint32_t element = 0;
                 element < length.value();
                 ++element) {
                auto opcode = expect_opcode(
                    reader,
                    element_opcode,
                    "Scene property array element is invalid");
                if (!opcode.has_value()) {
                    return opcode;
                }
                auto value = skip_word(
                    reader,
                    "Scene property array element is truncated");
                if (!value.has_value()) {
                    return value;
                }
            }
            auto end = expect_opcode(
                reader,
                kEndArray,
                "Scene property array is not terminated");
            if (!end.has_value()) {
                return end;
            }
        } else if (
            kind.value() == 0x0cU ||
            kind.value() == 0x0eU ||
            kind.value() == 0x10U) {
            auto value_opcode = expect_opcode(
                reader,
                kString,
                "Scene property string definition is invalid");
            if (!value_opcode.has_value()) {
                return value_opcode;
            }
            auto value = skip_word(
                reader,
                "Scene property string definition is truncated");
            if (!value.has_value()) {
                return value;
            }
        } else if (kind.value() == 0x11U) {
            auto values_opcode = expect_opcode(
                reader,
                kContainer,
                "Scene property string table is invalid");
            if (!values_opcode.has_value()) {
                return values_opcode;
            }
            const auto values = reader.read_u32();
            if (!values.has_value()) {
                return core::Result<void, ScenePlacementError>::failure(
                    truncated_error(
                        values.error(),
                        "Scene property string table count is truncated"));
            }
            if (values.value() > limits.max_tokens) {
                return core::Result<void, ScenePlacementError>::failure(
                    {
                        ScenePlacementErrorCode::limit_exceeded,
                        reader.offset() - 4U,
                        "Scene property string table exceeds configured limits"
                    });
            }
            for (std::uint32_t value = 0;
                 value < values.value();
                 ++value) {
                auto value_opcode = expect_opcode(
                    reader,
                    kString,
                    "Scene property string table entry is invalid");
                if (!value_opcode.has_value()) {
                    return value_opcode;
                }
                auto value_index = skip_word(
                    reader,
                    "Scene property string table entry is truncated");
                if (!value_index.has_value()) {
                    return value_index;
                }
            }
        } else {
            return core::Result<void, ScenePlacementError>::failure(
                {
                    ScenePlacementErrorCode::unsupported_layout,
                    kind_offset,
                    "Scene property definition kind is unsupported"
                });
        }
    }
    return core::Result<void, ScenePlacementError>::success();
}

core::Result<std::vector<Instruction>, ScenePlacementError>
read_instructions(
    binaryio::BinaryReader& reader,
    std::uint32_t flags,
    const ScenePlacementDecodeLimits& limits) {
    std::vector<Instruction> instructions;
    instructions.reserve(
        std::min<std::size_t>(reader.remaining(), 262'144));

    bool saw_end = false;
    while (reader.remaining() > 0) {
        if (instructions.size() >= limits.max_instructions) {
            return core::Result<
                std::vector<Instruction>,
                ScenePlacementError>::failure(
                {
                    ScenePlacementErrorCode::limit_exceeded,
                    reader.offset(),
                    "Scene property instruction count exceeds configured limits"
                });
        }
        Instruction instruction;
        instruction.offset = reader.offset();
        const auto opcode = reader.read_u8();
        if (!opcode.has_value()) {
            return core::Result<
                std::vector<Instruction>,
                ScenePlacementError>::failure(
                truncated_error(
                    opcode.error(),
                    "Scene property instruction is truncated"));
        }
        instruction.opcode = opcode.value();
        const auto base = base_opcode(instruction.opcode);

        if (
            base == kArray ||
            base == kContainer ||
            base == kInt32 ||
            base == kString ||
            base == kBitfield ||
            base == kEnum ||
            base == kFloat32) {
            const auto word = reader.read_u32();
            if (!word.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        word.error(),
                        "Scene property instruction operand is truncated"));
            }
            instruction.word = word.value();
            if (base == kFloat32) {
                instruction.floating =
                    std::bit_cast<float>(word.value());
            }
        } else if (
            base == kChar ||
            base == kBool ||
            base == kInt8) {
            const auto value = reader.read_u8();
            if (!value.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        value.error(),
                        "Scene property byte operand is truncated"));
            }
            instruction.word = value.value();
            if (base == kBool && instruction.word > 1U) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    {
                        ScenePlacementErrorCode::invalid_instruction,
                        instruction.offset,
                        "Scene property boolean operand is invalid"
                    });
            }
        } else if (base == kInt16) {
            const auto value = reader.read_u16();
            if (!value.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        value.error(),
                        "Scene property integer operand is truncated"));
            }
            instruction.word = value.value();
        } else if (base == kFloat64) {
            const auto value = reader.read_bytes(8);
            if (!value.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        value.error(),
                        "Scene property double operand is truncated"));
            }
        } else if (base == kRawData) {
            const auto length = reader.read_u32();
            if (!length.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        length.error(),
                        "Scene property raw-data length is truncated"));
            }
            const auto raw = reader.read_bytes(length.value());
            if (!raw.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        raw.error(),
                        "Scene property raw data is truncated"));
            }
        } else if (base == kStringArray) {
            const auto count = reader.read_u32();
            if (!count.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        count.error(),
                        "Scene property string-array count is truncated"));
            }
            if ((flags & (1U << 3U)) == 0U ||
                count.value() >
                    std::numeric_limits<std::size_t>::max() /
                        sizeof(std::uint32_t)) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    {
                        ScenePlacementErrorCode::unsupported_layout,
                        instruction.offset,
                        "Inline scene property strings are unsupported"
                    });
            }
            const auto values = reader.read_bytes(
                static_cast<std::size_t>(count.value()) *
                sizeof(std::uint32_t));
            if (!values.has_value()) {
                return core::Result<
                    std::vector<Instruction>,
                    ScenePlacementError>::failure(
                    truncated_error(
                        values.error(),
                        "Scene property string array is truncated"));
            }
        } else if (
            base != kBeginObject &&
            base != kReference &&
            base != kEndArray &&
            base != kSkipMark &&
            base != kEndObject &&
            base != kEndOfStream) {
            return core::Result<
                std::vector<Instruction>,
                ScenePlacementError>::failure(
                {
                    ScenePlacementErrorCode::invalid_instruction,
                    instruction.offset,
                    "Scene property instruction opcode is unsupported"
                });
        }

        instructions.push_back(instruction);
        if (base == kEndOfStream) {
            saw_end = true;
            break;
        }
    }
    if (!saw_end) {
        return core::Result<
            std::vector<Instruction>,
            ScenePlacementError>::failure(
            {
                ScenePlacementErrorCode::truncated,
                reader.offset(),
                "Scene property instruction stream is not terminated"
            });
    }
    return core::Result<
        std::vector<Instruction>,
        ScenePlacementError>::success(
        std::move(instructions));
}

}

core::Result<ScenePlacementDecodeResult, ScenePlacementError>
ScenePlacementDecoder::decode(
    std::span<const std::byte> bytes,
    ScenePlacementDecodeLimits limits) {
    if (bytes.size() > limits.max_file_size) {
        return failure(
            ScenePlacementErrorCode::limit_exceeded,
            0,
            "Scene property file exceeds configured limits");
    }
    if (bytes.size() < kHeaderSize) {
        return failure(
            ScenePlacementErrorCode::truncated,
            0,
            "Scene property header is truncated");
    }

    binaryio::BinaryReader reader(bytes);
    const auto magic = reader.read_bytes(14);
    if (!magic.has_value()) {
        return failure(
            ScenePlacementErrorCode::truncated,
            0,
            "Scene property signature is truncated");
    }
    for (std::size_t index = 0; index < kMagic.size(); ++index) {
        if (magic.value()[index] !=
            static_cast<std::byte>(kMagic[index])) {
            return failure(
                ScenePlacementErrorCode::invalid_header,
                index,
                "Scene property signature is invalid");
        }
    }
    if (magic.value()[kMagic.size()] != std::byte{0}) {
        return failure(
            ScenePlacementErrorCode::invalid_header,
            kMagic.size(),
            "Scene property signature is not terminated");
    }

    const auto raw = reader.read_u8();
    const auto flags = reader.read_u32();
    const auto reserved = reader.read_u32();
    const auto token_count = reader.read_u32();
    const auto data_offset = reader.read_u32();
    if (!raw || !flags || !reserved || !token_count || !data_offset) {
        return failure(
            ScenePlacementErrorCode::truncated,
            reader.offset(),
            "Scene property header is truncated");
    }
    if (raw.value() != 0U ||
        (flags.value() & (1U << 3U)) == 0U) {
        return failure(
            ScenePlacementErrorCode::unsupported_layout,
            14,
            "Scene property header layout is unsupported");
    }
    if (token_count.value() > limits.max_tokens) {
        return failure(
            ScenePlacementErrorCode::limit_exceeded,
            23,
            "Scene property token count exceeds configured limits");
    }
    if (data_offset.value() > bytes.size() - kHeaderSize) {
        return failure(
            ScenePlacementErrorCode::truncated,
            27,
            "Scene property token table exceeds the file");
    }

    const auto token_region = bytes.subspan(
        kHeaderSize,
        data_offset.value());
    std::size_t terminators = 0;
    for (const auto value : token_region) {
        if (value == std::byte{0}) {
            ++terminators;
        }
    }
    if (terminators < static_cast<std::size_t>(token_count.value()) + 1U) {
        return failure(
            ScenePlacementErrorCode::truncated,
            kHeaderSize,
            "Scene property token table is truncated");
    }

    const auto object_count_offset =
        kHeaderSize + static_cast<std::size_t>(data_offset.value());
    const auto seek = reader.seek(object_count_offset);
    if (!seek.has_value()) {
        return failure(
            ScenePlacementErrorCode::truncated,
            object_count_offset,
            "Scene property object table is truncated");
    }
    const auto declared_objects = reader.read_u32();
    if (!declared_objects.has_value()) {
        return failure(
            ScenePlacementErrorCode::truncated,
            object_count_offset,
            "Scene property object count is truncated");
    }

    auto definitions = skip_definitions(reader, limits);
    if (!definitions.has_value()) {
        return core::Result<
            ScenePlacementDecodeResult,
            ScenePlacementError>::failure(
            definitions.error());
    }
    auto instructions = read_instructions(
        reader,
        flags.value(),
        limits);
    if (!instructions.has_value()) {
        return core::Result<
            ScenePlacementDecodeResult,
            ScenePlacementError>::failure(
            instructions.error());
    }

    ScenePlacementDecodeResult result;
    result.declared_objects = declared_objects.value();
    const auto& stream = instructions.value();
    constexpr std::size_t kPlacementInstructionCount = 20;
    for (std::size_t index = 0;
         index + kPlacementInstructionCount <= stream.size();
         ++index) {
        if (
            !is_opcode(stream[index], kBeginObject) ||
            !is_opcode(stream[index + 1U], kEnum) ||
            !is_opcode(stream[index + 2U], kArray) ||
            stream[index + 2U].word != 9U ||
            !is_opcode(stream[index + 12U], kEndArray) ||
            !is_opcode(stream[index + 13U], kArray) ||
            stream[index + 13U].word != 3U ||
            !is_opcode(stream[index + 17U], kEndArray) ||
            !is_opcode(stream[index + 18U], kBool) ||
            !is_opcode(stream[index + 19U], kInt32)) {
            continue;
        }
        bool valid_floats = true;
        for (std::size_t matrix = 0; matrix < 9; ++matrix) {
            valid_floats =
                valid_floats &&
                is_opcode(stream[index + 3U + matrix], kFloat32) &&
                std::isfinite(
                    stream[index + 3U + matrix].floating);
        }
        for (std::size_t position = 0; position < 3; ++position) {
            valid_floats =
                valid_floats &&
                is_opcode(stream[index + 14U + position], kFloat32) &&
                std::isfinite(
                    stream[index + 14U + position].floating);
        }
        if (!valid_floats) {
            continue;
        }
        if (result.placements.size() >= limits.max_placements) {
            return failure(
                ScenePlacementErrorCode::limit_exceeded,
                stream[index].offset,
                "Scene placement count exceeds configured limits");
        }

        ScenePlacement placement;
        placement.primitive_record = stream[index + 19U].word;
        placement.inactive = stream[index + 18U].word != 0U;
        placement.byte_offset = stream[index].offset;
        for (std::size_t matrix = 0; matrix < 9; ++matrix) {
            placement.matrix[matrix] =
                stream[index + 3U + matrix].floating;
        }
        for (std::size_t position = 0; position < 3; ++position) {
            placement.position[position] =
                stream[index + 14U + position].floating;
        }
        result.placements.push_back(placement);
    }

    if (result.placements.empty()) {
        return failure(
            ScenePlacementErrorCode::no_placements,
            reader.offset(),
            "Scene property stream contains no supported placements");
    }
    return core::Result<
        ScenePlacementDecodeResult,
        ScenePlacementError>::success(
        std::move(result));
}

}
