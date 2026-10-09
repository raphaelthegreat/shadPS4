// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/info.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/pixel_format.h"

namespace Shader {

enum class SpecializationType : u64 {
    Buffer = 1,
    Image,
    Sampler,
    Fmask,
    VsAttrib,
};

struct VsAttribSpecialization {
    SpecializationType type : 3;
    u64 dst_select : 16;
    u64 num_class : 2;
    u64 reserved : 43;
};

struct BufferSpecialization {
    SpecializationType type : 3;
    u64 stride : 14;
    u64 swizzle_enable : 1;
    u64 index_stride : 2;
    u64 element_size : 2;
    u64 is_formatted : 1;
    u64 data_format : 4;
    u64 num_format : 3;
    u64 num_conversion : 3;
    u64 dst_select : 16;
    u64 reserved : 15;
};

struct ImageSpecialization {
    SpecializationType type : 3;
    u64 image_type : 4;
    u64 is_integer : 1;
    u64 is_storage : 1;
    u64 is_srgb : 1;
    u64 num_bindings : 5;
    u64 num_conversion : 3;
    u64 dst_select : 16;
    u32 reserved : 16;
};

struct FMaskSpecialization {
    SpecializationType type : 3;
    u64 width : 16;
    u64 height : 16;
    u64 reserved : 29;
};

struct SamplerSpecialization {
    SpecializationType type : 3;
    u64 force_degamma : 1;
    u64 force_unnormalized : 1;
    u64 reserved : 59;
};

union SpecializationEntry {
    u64 raw;
    BufferSpecialization buffer;
    ImageSpecialization image;
    SamplerSpecialization sampler;
    FMaskSpecialization fmask;
    VsAttribSpecialization vs_attrib;
};
static_assert(sizeof(SpecializationEntry) == sizeof(u64));

/**
 * Alongside runtime information, this structure also checks bound resources
 * for compatibility. Can be used as a key for storing shader permutations.
 * Is separate from runtime information, because resource layout can only be deduced
 * after the first compilation of a module.
 */
struct StageSpecialization {
    static constexpr size_t MAX_BINDINGS = 128;

    struct Bitset {
        void Set(u32 index) noexcept {
            bits[index / 64] |= u64{1} << (index & 63);
        }

        bool Read(u32 index) const noexcept {
            return bits[index / 64] & (u64{1} << (index & 63));
        }

        std::array<u64, MAX_BINDINGS / 64> bits;
    };

    RuntimeInfo runtime_info;
    u32 num_entries;
    u32 start_binding;
    Bitset bitset;
    std::array<SpecializationEntry, MAX_BINDINGS> entries;

    StageSpecialization() = default;
    StageSpecialization(const Info& info, RuntimeInfo runtime_info_,
                        const Gcn::FetchShaderData& fetch_shader_data,
                        u32 start_binding_)
        : runtime_info{runtime_info_}, start_binding{info.NumBindings() ? start_binding_ : 0} {
            u32 index{};
            Bitset bits{};
            for (const auto& desc : info.buffers) {
                auto& spec = entries[index++];
                const auto sharp = desc.GetSharp(info);
                if (!sharp) {
                    spec.raw = 0;
                    continue;
                }
                bits.Set(index - 1);
                spec.buffer = BufferSpecialization{
                    .type = SpecializationType::Buffer,
                    .stride = sharp.GetStride(),
                    .swizzle_enable = sharp.swizzle_enable,
                    .index_stride = sharp.swizzle_enable ? sharp.index_stride : 0u,
                    .element_size = sharp.swizzle_enable ? sharp.element_size : 0u,
                    .is_formatted = desc.is_formatted,
                    .data_format = desc.is_formatted ? u64(sharp.GetDataFmt()) : 0u,
                    .num_format = desc.is_formatted ? u64(sharp.GetNumberFmt()) : 0u,
                    .num_conversion = desc.is_formatted ? u64(sharp.GetNumberConversion()) : 0u,
                    .dst_select = desc.is_formatted ? sharp.DstSelect().raw : 0u,
                };
            }
            for (const auto& desc : info.images) {
                auto& spec = entries[index++];
                const auto sharp = desc.GetSharp(info);
                if (!sharp) {
                    spec.raw = 0;
                    continue;
                }
                bits.Set(index - 1);
                spec.image = ImageSpecialization{
                    .type = SpecializationType::Image,
                    .image_type = u64(sharp.GetViewType(desc.is_array)),
                    .is_integer = AmdGpu::IsInteger(sharp.GetNumberFmt()),
                    .is_storage = desc.is_written,
                    .is_srgb = desc.is_written ? false : sharp.GetNumberFmt() == AmdGpu::NumberFormat::Srgb,
                    .num_bindings = desc.NumBindings(sharp),
                    .num_conversion = u64(sharp.GetNumberConversion()),
                    .dst_select = desc.is_written ? sharp.DstSelect().raw : 0u,
               };
            }
            for (const auto& desc : info.samplers) {
                auto& spec = entries[index++];
                const auto sharp = desc.GetSharp(info);
                if (!sharp) {
                    spec.raw = 0;
                    continue;
                }
                bits.Set(index - 1);
                spec.sampler = SamplerSpecialization{
                    .type = SpecializationType::Sampler,
                    .force_degamma = u16(sharp.force_degamma),
                    .force_unnormalized = u16(sharp.force_unnormalized),
                };
            }
        if (info.sw_stage == SwStage::Vertex && !fetch_shader_data.Empty()) {
            // Specialize shader on VS input number types to follow spec.
            for (const auto& desc : fetch_shader_data.attributes) {
                auto& spec = entries[index++];
                const auto sharp = desc.GetSharp(info);
                if (!sharp) {
                    spec.raw = 0;
                    continue;
                }
                bits.Set(index - 1);
                spec.vs_attrib = VsAttribSpecialization{
                    .type = SpecializationType::VsAttrib,
                    .dst_select = sharp.DstSelect().raw,
                    .num_class = u64(AmdGpu::GetNumberClass(sharp.GetNumberFmt())),
                };
            }
        }

        for (const auto& desc : info.fmasks) {
            auto& spec = entries[index++];
            const auto sharp = desc.GetSharp(info);
            if (!sharp) {
                spec.raw = 0;
                continue;
            }
            bits.Set(index - 1);
            spec.fmask = FMaskSpecialization{
                .type = SpecializationType::Fmask,
                .width = sharp.width,
                .height = sharp.height,
            };
        }

        ASSERT(index < MAX_BINDINGS);
        num_entries = index;
        bitset = bits;

        // Initialize runtime_info fields that rely on analysis in tessellation passes
        if (info.sw_stage == SwStage::TessellationControl ||
            info.sw_stage == SwStage::TessellationEval) {
            TessellationDataConstantBuffer tess_constants{};
            info.ReadTessConstantBuffer(tess_constants);
            runtime_info.InitFromTessConstants(tess_constants);
        }
    }

    friend bool operator==(const StageSpecialization& existing, const StageSpecialization& current) noexcept {
        if (current.runtime_info != existing.runtime_info) {
            return false;
        }
        if (current.num_entries != existing.num_entries) {
            return false;
        }
        if (current.start_binding != existing.start_binding) {
            return false;
        }
        bool equivalent = true;
        for (u32 i = 0; i < current.num_entries; i++) {
            equivalent &= !current.bitset.Read(i) || current.entries[i].raw == existing.entries[i].raw;
        }
        return equivalent;
    }

    void Serialize(Serialization::Archive& ar) const;
    bool Deserialize(Serialization::Archive& ar);
};

} // namespace Shader
