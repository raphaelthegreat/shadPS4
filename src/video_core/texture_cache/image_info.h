// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>
#include <type_traits>

#include "common/types.h"
#include "video_core/amdgpu/cb_db_extent.h"
#include "video_core/amdgpu/tiling.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/tile.h"
#include "video_core/texture_cache/types.h"

namespace AmdGpu {
struct ColorBuffer;
struct DepthBuffer;
struct Image;
enum class ImageType : u64;
} // namespace AmdGpu

namespace Libraries::VideoOut {
struct BufferAttributeGroup;
}

namespace Shader {
struct ImageResource;
}

namespace VideoCore {

struct ImageProperties {
    u32 is_volume : 1;
    u32 is_tiled : 1;
    u32 is_pow2 : 1;
    u32 is_block : 1;
    u32 is_depth : 1;
    u32 has_stencil : 1;
};

/// Every image the guest can hand over begins on a 256 byte boundary, so an address within the
/// 40 bit address space is carried around shifted down by this much and still fits in a word.
constexpr u32 IMAGE_ADDRESS_SHIFT = 8;

/// The aspects of an image that decide whether a cached one can serve a request as it stands.
/// Two images sharing a key hold the same texels, in the same format, in the same place, so a
/// request matching one is answered without asking anything further of it.
///
/// Everything in it is read straight out of a descriptor, so that building one never costs the
/// walk over the mip chain that the sizes and per mip tiling of an image come from. Those follow
/// from the key anyway, as does the array mode, which is why neither is kept. The tile mode is
/// compared even though it says nothing about where texels land, since an image turned away by it
/// only loses a shortcut and still reaches the overlap handling, which is where aliasing a depth
/// buffer as colour is meant to be sorted out.
struct ImageKey {
    u32 guest_address; ///< Shifted down by the alignment every image address carries
    u32 pixel_format;
    u32 flags; ///< Everything narrow enough to share a word, see ImageInfo::Key
    u16 pitch;
    u16 width;
    u16 height;
    u16 depth;
    u16 levels;
    u16 layers;

    /// Compared as raw bytes, which the layout above is packed to allow: it is free of padding
    /// and so every key has a single representation.
    bool operator==(const ImageKey& other) const noexcept {
        return std::memcmp(this, &other, sizeof(ImageKey)) == 0;
    }
};
static_assert(sizeof(ImageKey) == 24 && std::has_unique_object_representations_v<ImageKey>);

struct ImageInfo {
    ImageInfo() = default;
    ImageInfo(const Libraries::VideoOut::BufferAttributeGroup& group, VAddr cpu_address) noexcept;
    ImageInfo(const AmdGpu::ColorBuffer& buffer, AmdGpu::CbDbExtent hint) noexcept;
    ImageInfo(const AmdGpu::DepthBuffer& buffer, u32 num_slices, VAddr htile_address,
              AmdGpu::CbDbExtent hint, bool write_buffer = false) noexcept;
    ImageInfo(const AmdGpu::Image& sharp, const Shader::ImageResource& desc) noexcept;

    bool IsTiled() const {
        return tile_mode != AmdGpu::TileMode::DisplayLinearAligned;
    }

    void UpdateSize() noexcept;

    [[nodiscard]] ImageKey Key() const noexcept;

    struct {
        VAddr cmask_addr;
        VAddr fmask_addr;
        VAddr htile_addr;
        s32 htile_clear_mask = -1;
    } meta_info{};

    ImageProperties props;
    vk::Format pixel_format;
    AmdGpu::ImageType type;
    SubresourceExtent resources;
    Extent3D size;
    u32 num_bits;
    u32 num_samples;
    u32 pitch;
    AmdGpu::TileMode tile_mode;
    AmdGpu::ArrayMode array_mode;
    std::array<MipInfo, MAX_MIPS> mips_layout;
    u32 micro_tiled_mips{};
    VAddr guest_address;
    u32 guest_size;
    u8 bank_swizzle{};
    bool alt_tile{};
    VAddr stencil_addr{};
    u32 stencil_size{};
};

} // namespace VideoCore
