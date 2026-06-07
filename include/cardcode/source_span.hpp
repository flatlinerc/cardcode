#pragma once

#include <cstdint>

namespace cardcode {

// Byte/line/column range into the original UTF-8 source text. v1 source is
// ASCII-compatible, so byte offsets and column counts coincide.
struct SourceSpan {
    std::uint32_t start_offset{};
    std::uint32_t end_offset{};
    std::uint32_t start_line{};
    std::uint32_t start_column{};
    std::uint32_t end_line{};
    std::uint32_t end_column{};
};

} // namespace cardcode
