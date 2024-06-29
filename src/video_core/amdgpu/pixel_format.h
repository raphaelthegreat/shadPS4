// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string_view>
#include <fmt/format.h>
#include "common/types.h"

namespace AmdGpu {

// Table 8.13 Data and Image Formats [Sea Islands Series Instruction Set Architecture]
enum class DataFormat : u32 {
    FormatInvalid = 0,
    Format8 = 1,
    Format16 = 2,
    Format8_8 = 3,
    Format32 = 4,
    Format16_16 = 5,
    Format10_11_11 = 6,
    Format11_11_10 = 7,
    Format10_10_10_2 = 8,
    Format2_10_10_10 = 9,
    Format8_8_8_8 = 10,
    Format32_32 = 11,
    Format16_16_16_16 = 12,
    Format32_32_32 = 13,
    Format32_32_32_32 = 14,
    Format5_6_5 = 16,
    Format1_5_5_5 = 17,
    Format5_5_5_1 = 18,
    Format4_4_4_4 = 19,
    Format8_24 = 20,
    Format24_8 = 21,
    FormatX24_8_32 = 22,
    FormatGB_GR = 32,
    FormatBG_RG = 33,
    Format5_9_9_9 = 34,
    FormatBc1 = 35,
    FormatBc2 = 36,
    FormatBc3 = 37,
    FormatBc4 = 38,
    FormatBc5 = 39,
    FormatBc6 = 40,
    FormatBc7 = 41,
};

enum class NumberFormat : u32 {
    Unorm = 0,
    Snorm = 1,
    Uscaled = 2,
    Sscaled = 3,
    Uint = 4,
    Sint = 5,
    SnormNz = 6,
    Float = 7,
    Srgb = 9,
    Ubnorm = 10,
    UbnromNz = 11,
    Ubint = 12,
    Ubscaled = 13,
};

enum class ImageType : u64 {
    Buffer = 0,
    Color1D = 8,
    Color2D = 9,
    Color3D = 10,
    Cube = 11,
    Color1DArray = 12,
    Color2DArray = 13,
    Color2DMsaa = 14,
    Color2DMsaaArray = 15,
};

constexpr std::string_view NameOf(ImageType type) {
    switch (type) {
    case ImageType::Buffer:
        return "Buffer";
    case ImageType::Color1D:
        return "Color1D";
    case ImageType::Color2D:
        return "Color2D";
    case ImageType::Color3D:
        return "Color3D";
    case ImageType::Cube:
        return "Cube";
    case ImageType::Color1DArray:
        return "Color1DArray";
    case ImageType::Color2DArray:
        return "Color2DArray";
    case ImageType::Color2DMsaa:
        return "Color2DMsaa";
    case ImageType::Color2DMsaaArray:
        return "Color2DMsaaArray";
    default:
        return "Unknown";
    }
}

[[nodiscard]] std::string_view NameOf(NumberFormat fmt);

int NumComponents(DataFormat format);
int NumBits(DataFormat format);

} // namespace AmdGpu

template <>
struct fmt::formatter<AmdGpu::NumberFormat> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }
    auto format(AmdGpu::NumberFormat fmt, format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", AmdGpu::NameOf(fmt));
    }
};

template <>
struct fmt::formatter<AmdGpu::ImageType> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin();
    }
    auto format(AmdGpu::ImageType type, format_context& ctx) const {
        return fmt::format_to(ctx.out(), "{}", AmdGpu::NameOf(type));
    }
};

