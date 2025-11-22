// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/buffer_cache/buffer.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/image.h"

namespace Vulkan {

static constexpr size_t DeviceBufferSize = 128_MB;

constexpr static vk::AccessFlags2 AccessReadMask =
    vk::AccessFlagBits2::eIndexRead | vk::AccessFlagBits2::eVertexAttributeRead |
    vk::AccessFlagBits2::eUniformRead | vk::AccessFlagBits2::eShaderRead |
    vk::AccessFlagBits2::eColorAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentRead |
    vk::AccessFlagBits2::eTransferRead | vk::AccessFlagBits2::eMemoryRead;

constexpr static vk::AccessFlags2 AccessWriteMask =
    vk::AccessFlagBits2::eShaderWrite | vk::AccessFlagBits2::eColorAttachmentWrite |
    vk::AccessFlagBits2::eDepthStencilAttachmentWrite | vk::AccessFlagBits2::eTransferWrite |
    vk::AccessFlagBits2::eMemoryWrite;

static std::pair<u32, u32> SanitizeCopyLayers(const VideoCore::ImageInfo& src,
                                              const VideoCore::ImageInfo& dst, u32 depth) {
    u32 src_layers = src.resources.layers;
    u32 dst_layers = dst.resources.layers;

    // 3D images can only use 1 layer.
    if (src.type == AmdGpu::ImageType::Color3D && src_layers != 1) {
        LOG_WARNING(Render_Vulkan, "Coercing copy 3D source layers {} to 1.", src_layers);
        src_layers = 1;
    }
    if (dst.type == AmdGpu::ImageType::Color3D && dst_layers != 1) {
        LOG_WARNING(Render_Vulkan, "Coercing copy 3D destination layers {} to 1.", dst_layers);
        dst_layers = 1;
    }

    // If the image type is equal, layer count must match. Take the minimum of both.
    if (src.type == dst.type && src_layers != dst_layers) {
        LOG_WARNING(Render_Vulkan,
                    "Coercing copy source layers {} and destination layers {} to minimum.",
                    src_layers, dst_layers);
        src_layers = dst_layers = std::min(src_layers, dst_layers);
    } else {
        // For 2D <-> 3D copies, 2D layer count must equal 3D depth.
        if (src.type == AmdGpu::ImageType::Color2D && dst.type == AmdGpu::ImageType::Color3D &&
            src_layers != depth) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy 2D source layers {} to 3D destination depth {}", src_layers,
                        depth);
            src_layers = depth;
        }
        if (src.type == AmdGpu::ImageType::Color3D && dst.type == AmdGpu::ImageType::Color2D &&
            dst_layers != depth) {
            LOG_WARNING(Render_Vulkan,
                        "Coercing copy 2D destination layers {} to 3D source depth {}", dst_layers,
                        depth);
            dst_layers = depth;
        }
    }

    return std::make_pair(src_layers, dst_layers);
}

Runtime::Runtime(const Instance& instance_, Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_}, blit_helper{instance, scheduler},
      copy_buffer{instance, scheduler, VideoCore::MemoryUsage::DeviceLocal, DeviceBufferSize} {
    memory_barrier.dstStageMask = vk::PipelineStageFlagBits2::eAllCommands;
    memory_barrier.dstAccessMask =
        vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
}

void Runtime::CopyBuffer(const VideoCore::Buffer& src, const VideoCore::Buffer& dst,
                         std::span<const vk::BufferCopy> copies) {
    scheduler.EndRendering();

    bool needs_flush{};
    for (const auto& copy : copies) {
        needs_flush |= IsBufferAccessed(src, copy.srcOffset, copy.size);
        needs_flush |= IsBufferAccessed(dst, copy.dstOffset, copy.size, true);
    }
    if (needs_flush) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(src.Handle(), dst.Handle(), copies);

    for (const auto& copy : copies) {
        AccessBuffer(src, copy.srcOffset, copy.size, vk::PipelineStageFlagBits2::eTransfer,
                     vk::AccessFlagBits2::eTransferRead);
        AccessBuffer(dst, copy.dstOffset, copy.size, vk::PipelineStageFlagBits2::eTransfer,
                     vk::AccessFlagBits2::eTransferWrite);
    }
}

void Runtime::CopyBuffer(const VideoCore::Buffer& src, const VideoCore::Buffer& dst,
                         vk::BufferCopy copy) {
    scheduler.EndRendering();

    bool needs_flush = IsBufferAccessed(src, copy.srcOffset, copy.size);
    needs_flush |= IsBufferAccessed(dst, copy.dstOffset, copy.size, true);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBuffer(src.Handle(), dst.Handle(), copy);

    AccessBuffer(src, copy.srcOffset, copy.size, vk::PipelineStageFlagBits2::eTransfer,
                 vk::AccessFlagBits2::eTransferRead);
    AccessBuffer(dst, copy.dstOffset, copy.size, vk::PipelineStageFlagBits2::eTransfer,
                 vk::AccessFlagBits2::eTransferWrite);
}

void Runtime::FillBuffer(const VideoCore::Buffer& dst, u64 offset, u64 size, u32 value) {
    scheduler.EndRendering();

    if (IsBufferAccessed(dst, offset, size, true)) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.fillBuffer(dst.Handle(), offset, size, value);

    AccessBuffer(dst, offset, size, vk::PipelineStageFlagBits2::eTransfer,
                 vk::AccessFlagBits2::eTransferWrite);
}

void Runtime::InlineData(VideoCore::Buffer& dst, u64 offset, u32 value) {
    scheduler.EndRendering();

    if (IsBufferAccessed(dst, offset, sizeof(value), true)) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.updateBuffer(dst.Handle(), offset, sizeof(value), &value);

    AccessBuffer(dst, offset, sizeof(value), vk::PipelineStageFlagBits2::eTransfer,
                 vk::AccessFlagBits2::eTransferWrite);
}

void Runtime::UploadImage(VideoCore::Image& dst, VideoCore::Buffer& src, u64 copy_size,
                          std::span<const vk::BufferImageCopy> upload_copies) {
    SetBackingSamples(dst, dst.info.num_samples, false);
    scheduler.EndRendering();

    const u64 offset = upload_copies.front().bufferOffset;
    bool needs_flush = IsBufferAccessed(src, offset, copy_size);
    needs_flush |= TransitionImageLayout(dst, vk::ImageLayout::eTransferDstOptimal,
                                         vk::PipelineStageFlagBits2::eCopy,
                                         vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyBufferToImage(src.Handle(), dst.GetImage(), vk::ImageLayout::eTransferDstOptimal,
                             upload_copies);
}

void Runtime::DownloadImage(VideoCore::Image& src, VideoCore::Buffer& dst, u64 copy_size,
                            std::span<const vk::BufferImageCopy> download_copies) {
    SetBackingSamples(src, src.info.num_samples, false);
    scheduler.EndRendering();

    const u64 offset = download_copies.front().bufferOffset;
    bool needs_flush = IsBufferAccessed(dst, offset, copy_size, true);
    needs_flush |= TransitionImageLayout(src, vk::ImageLayout::eTransferSrcOptimal,
                                         vk::PipelineStageFlagBits2::eCopy,
                                         vk::AccessFlagBits2::eTransferRead);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyImageToBuffer(src.GetImage(), vk::ImageLayout::eTransferSrcOptimal, dst.Handle(),
                             download_copies);

    AccessBuffer(dst, offset, copy_size, vk::PipelineStageFlagBits2::eCopy,
                 vk::AccessFlagBits2::eTransferWrite);
}

void Runtime::CopyImage(VideoCore::Image& src, VideoCore::Image& dst) {
    const u32 num_mips = std::min(src.info.resources.levels, dst.info.resources.levels);
    ASSERT(src.info.resources.layers == dst.info.resources.layers || num_mips == 1);

    const u32 width = src.info.size.width;
    const u32 height = src.info.size.height;
    const u32 depth =
        dst.info.type == AmdGpu::ImageType::Color3D ? dst.info.size.depth : src.info.size.depth;

    SetBackingSamples(dst, dst.info.num_samples, false);
    SetBackingSamples(src, src.info.num_samples);
    scheduler.EndRendering();

    boost::container::small_vector<vk::ImageCopy, 8> image_copies;
    for (u32 mip = 0; mip < num_mips; ++mip) {
        const auto mip_w = std::max(width >> mip, 1u);
        const auto mip_h = std::max(height >> mip, 1u);
        const auto mip_d = std::max(depth >> mip, 1u);
        const auto [src_layers, dst_layers] = SanitizeCopyLayers(src.info, dst.info, mip_d);

        image_copies.emplace_back(vk::ImageCopy{
            .srcSubresource{
                .aspectMask = src.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = src_layers,
            },
            .dstSubresource{
                .aspectMask = dst.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
                .mipLevel = mip,
                .baseArrayLayer = 0,
                .layerCount = dst_layers,
            },
            .extent = {mip_w, mip_h, mip_d},
        });
    }

    bool needs_flush = TransitionImageLayout(src, vk::ImageLayout::eTransferSrcOptimal,
                                             vk::PipelineStageFlagBits2::eCopy,
                                             vk::AccessFlagBits2::eTransferRead);
    needs_flush |= TransitionImageLayout(dst, vk::ImageLayout::eTransferDstOptimal,
                                         vk::PipelineStageFlagBits2::eCopy,
                                         vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyImage(src.GetImage(), src.backing->state.layout, dst.GetImage(),
                     dst.backing->state.layout, image_copies);

    dst.flags |= VideoCore::ImageFlagBits::GpuModified;
    dst.flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::CopyImageWithBuffer(VideoCore::Image& src, VideoCore::Image& dst,
                                  const VideoCore::Buffer& buffer, u64 offset) {
    ASSERT(src.info.resources == dst.info.resources || src.info.resources.levels == 1);

    SetBackingSamples(dst, dst.info.num_samples, false);
    SetBackingSamples(src, src.info.num_samples);

    vk::BufferImageCopy buffer_copy = {
        .bufferOffset = offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource{
            .aspectMask = src.aspect_mask & ~vk::ImageAspectFlagBits::eStencil,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = src.info.resources.layers,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {src.info.size.width, src.info.size.height, src.info.size.depth},
    };

    scheduler.EndRendering();

    memory_barrier.srcStageMask |= vk::PipelineStageFlagBits2::eCopy;
    memory_barrier.srcAccessMask |= vk::AccessFlagBits2::eTransferRead;

    bool needs_flush = TransitionImageLayout(src, vk::ImageLayout::eTransferSrcOptimal,
                                             vk::PipelineStageFlagBits2::eCopy,
                                             vk::AccessFlagBits2::eTransferRead);
    needs_flush |= TransitionImageLayout(dst, vk::ImageLayout::eTransferDstOptimal,
                                         vk::PipelineStageFlagBits2::eCopy,
                                         vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyImageToBuffer(src.GetImage(), vk::ImageLayout::eTransferSrcOptimal, buffer.Handle(),
                             buffer_copy);

    memory_barrier.srcStageMask |= vk::PipelineStageFlagBits2::eCopy;
    memory_barrier.srcAccessMask |= vk::AccessFlagBits2::eTransferWrite;

    cmdbuf.pipelineBarrier2(vk::DependencyInfo{
        .dependencyFlags = vk::DependencyFlagBits::eByRegion,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &memory_barrier,
    });

    buffer_copy.imageSubresource.aspectMask = dst.aspect_mask & ~vk::ImageAspectFlagBits::eStencil;

    cmdbuf.copyBufferToImage(buffer.Handle(), dst.GetImage(), vk::ImageLayout::eTransferDstOptimal,
                             buffer_copy);
}

void Runtime::CopyColorAndDepth(VideoCore::Image& src, VideoCore::Image& dst) {
    if (src.info.num_samples == 1 && dst.info.num_samples == 1) {
        // Perform depth<->color copy using the intermediate copy buffer.
        if (instance.IsMaintenance8Supported()) {
            CopyImage(src, dst);
        } else {
            CopyImageWithBuffer(src, dst, copy_buffer, 0);
        }
    } else if (src.info.num_samples == 1 && dst.info.num_samples > 1 && dst.info.props.is_depth) {
        // Perform a rendering pass to transfer the channels of source as samples in dest.
        bool needs_flush = TransitionImageLayout(src, vk::ImageLayout::eShaderReadOnlyOptimal,
                                                 vk::PipelineStageFlagBits2::eFragmentShader,
                                                 vk::AccessFlagBits2::eShaderRead);
        needs_flush |= TransitionImageLayout(dst, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                                             vk::PipelineStageFlagBits2::eLateFragmentTests,
                                             vk::AccessFlagBits2::eDepthStencilAttachmentWrite);
        if (needs_flush) {
            FlushBarriers();
        }

        blit_helper.ReinterpretColorAsMsDepth(
            dst.info.size.width, dst.info.size.height, dst.info.num_samples, src.info.pixel_format,
            dst.info.pixel_format, src.GetImage(), dst.GetImage());
    } else {
        LOG_WARNING(Render_Vulkan, "Unimplemented depth overlap copy");
    }
}

void Runtime::CopyDepthStencil(VideoCore::Image& src, VideoCore::Image& dst,
                               const VideoCore::SubresourceRange& sub_range,
                               vk::ImageAspectFlags aspect_mask) {
    bool needs_flush = TransitionImageLayout(src, vk::ImageLayout::eTransferSrcOptimal,
                                             vk::PipelineStageFlagBits2::eCopy,
                                             vk::AccessFlagBits2::eTransferRead);
    needs_flush |= TransitionImageLayout(dst, vk::ImageLayout::eTransferDstOptimal,
                                         vk::PipelineStageFlagBits2::eCopy,
                                         vk::AccessFlagBits2::eTransferWrite);
    if (needs_flush) {
        FlushBarriers();
    }

    const vk::ImageCopy region = {
        .srcSubresource{
            .aspectMask = aspect_mask,
            .mipLevel = 0,
            .baseArrayLayer = sub_range.base.layer,
            .layerCount = sub_range.extent.layers,
        },
        .srcOffset = {0, 0, 0},
        .dstSubresource{
            .aspectMask = aspect_mask,
            .mipLevel = 0,
            .baseArrayLayer = sub_range.base.layer,
            .layerCount = sub_range.extent.layers,
        },
        .dstOffset = {0, 0, 0},
        .extent = {dst.info.size.width, dst.info.size.height, 1},
    };

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.copyImage(src.GetImage(), vk::ImageLayout::eTransferSrcOptimal, dst.GetImage(),
                     vk::ImageLayout::eTransferDstOptimal, region);

    dst.flags |= VideoCore::ImageFlagBits::GpuModified;
    dst.flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::ResolveImage(VideoCore::Image& src, VideoCore::Image& dst,
                           const VideoCore::SubresourceRange& src_range,
                           const VideoCore::SubresourceRange& dst_range) {
    SetBackingSamples(dst, 1, false);
    scheduler.EndRendering();

    const bool needs_resolve = src.backing->num_samples != 1;
    const auto dst_stage =
        needs_resolve ? vk::PipelineStageFlagBits2::eResolve : vk::PipelineStageFlagBits2::eCopy;

    bool needs_flush = TransitionImageLayout(src, vk::ImageLayout::eTransferSrcOptimal, dst_stage,
                                             vk::AccessFlagBits2::eTransferRead, src_range);
    needs_flush |= TransitionImageLayout(dst, vk::ImageLayout::eTransferDstOptimal, dst_stage,
                                         vk::AccessFlagBits2::eTransferWrite, dst_range);
    if (needs_flush) {
        FlushBarriers();
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    const auto [src_layers, dst_layers] = SanitizeCopyLayers(src.info, dst.info, 1);
    if (!needs_resolve) {
        const vk::ImageCopy region = {
            .srcSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = src_range.base.layer,
                .layerCount = src_layers,
            },
            .srcOffset = {0, 0, 0},
            .dstSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = dst_range.base.layer,
                .layerCount = dst_layers,
            },
            .dstOffset = {0, 0, 0},
            .extent = {src.info.size.width, src.info.size.height, 1},
        };
        cmdbuf.copyImage(src.GetImage(), vk::ImageLayout::eTransferSrcOptimal, dst.GetImage(),
                         vk::ImageLayout::eTransferDstOptimal, region);
    } else {
        const vk::ImageResolve region = {
            .srcSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = src_range.base.layer,
                .layerCount = src_layers,
            },
            .srcOffset = {0, 0, 0},
            .dstSubresource{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0,
                .baseArrayLayer = dst_range.base.layer,
                .layerCount = dst_layers,
            },
            .dstOffset = {0, 0, 0},
            .extent = {src.info.size.width, src.info.size.height, 1},
        };
        cmdbuf.resolveImage(src.GetImage(), vk::ImageLayout::eTransferSrcOptimal, dst.GetImage(),
                            vk::ImageLayout::eTransferDstOptimal, region);
    }

    dst.flags |= VideoCore::ImageFlagBits::GpuModified;
    dst.flags &= ~VideoCore::ImageFlagBits::Dirty;
}

void Runtime::ClearImage(VideoCore::Image& dst, const VideoCore::SubresourceRange& range,
                         const vk::ClearValue& clear_value) {
    scheduler.EndRendering();

    const bool needs_flush = TransitionImageLayout(dst, vk::ImageLayout::eTransferDstOptimal,
                                                   vk::PipelineStageFlagBits2::eClear,
                                                   vk::AccessFlagBits2::eTransferWrite, range);
    if (needs_flush) {
        FlushBarriers();
    }

    const vk::ImageSubresourceRange vk_range = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = range.base.level,
        .levelCount = range.extent.levels,
        .baseArrayLayer = range.base.layer,
        .layerCount = range.extent.layers,
    };
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.clearColorImage(dst.GetImage(), vk::ImageLayout::eTransferDstOptimal, clear_value.color,
                           vk_range);

    dst.flags |= VideoCore::ImageFlagBits::GpuModified;
    dst.flags &= ~VideoCore::ImageFlagBits::Dirty;
}

bool Runtime::IsBufferAccessed(const VideoCore::Buffer& buffer, u64 offset, u64 size,
                               bool check_read_access) {
    const AddressRange range = {
        .resource = std::bit_cast<u64>(buffer.Handle()),
        .range_start = buffer.CpuAddr() + offset,
        .range_end = buffer.CpuAddr() + offset + size - 1,
    };
    bool has_access = barrier_tracker.FindRange(range, Access::Write);
    if (check_read_access && !has_access) {
        has_access = barrier_tracker.FindRange(range, Access::Read);
    }
    return has_access;
}

bool Runtime::TransitionImageLayout(VideoCore::Image& image, vk::ImageLayout dst_layout,
                                    vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access,
                                    std::optional<VideoCore::SubresourceRange> subres_range) {
    return TransitionBackingLayout(image.backing, image.info.resources, image.aspect_mask,
                                   dst_layout, dst_stage, dst_access, subres_range);
}

bool Runtime::TransitionBackingLayout(VideoCore::BackingImage* backing,
                                      const VideoCore::SubresourceExtent& resources,
                                      vk::ImageAspectFlags aspect_mask, vk::ImageLayout dst_layout,
                                      vk::PipelineStageFlags2 dst_stage,
                                      vk::AccessFlags2 dst_access,
                                      std::optional<VideoCore::SubresourceRange> subres_range) {
    auto& last_state = backing->state;
    auto& subresource_states = backing->subresource_states;

    const auto prev_num_barriers = image_barriers.size();
    const bool needs_partial_transition =
        subres_range &&
        (subres_range->base != VideoCore::SubresourceBase{} || subres_range->extent != resources);
    const bool partially_transited = !subresource_states.empty();

    if (needs_partial_transition || partially_transited) {
        if (!partially_transited) {
            subresource_states.resize(resources.levels * resources.layers);
            std::ranges::fill(subresource_states, last_state);
        }

        // In case of partial transition, we need to change the specified subresources only.
        // Otherwise all subresources need to be set to the same state so we can use a full
        // resource transition for the next time.
        const auto base =
            needs_partial_transition ? subres_range->base : VideoCore::SubresourceBase{};
        const auto extent = needs_partial_transition ? subres_range->extent : resources;

        for (u32 mip = base.level; mip < base.level + extent.levels; mip++) {
            for (u32 layer = base.layer; layer < base.layer + extent.layers; layer++) {
                const u32 subres_idx = mip * resources.layers + layer;
                ASSERT(subres_idx < subresource_states.size());
                auto& state = subresource_states[subres_idx];

                if (state.layout != dst_layout || state.access_mask != dst_access) {
                    image_barriers.emplace_back(vk::ImageMemoryBarrier2{
                        .srcStageMask = state.pl_stage,
                        .srcAccessMask = state.access_mask,
                        .dstStageMask = dst_stage,
                        .dstAccessMask = dst_access,
                        .oldLayout = state.layout,
                        .newLayout = dst_layout,
                        .image = backing->image,
                        .subresourceRange{
                            .aspectMask = aspect_mask,
                            .baseMipLevel = mip,
                            .levelCount = 1,
                            .baseArrayLayer = layer,
                            .layerCount = 1,
                        },
                    });
                    state.layout = dst_layout;
                    state.access_mask = dst_access;
                    state.pl_stage = dst_stage;
                }
            }
        }

        if (!needs_partial_transition) {
            subresource_states.clear();
        }
    } else if (last_state.layout != dst_layout || last_state.access_mask != dst_access) {
        image_barriers.emplace_back(vk::ImageMemoryBarrier2{
            .srcStageMask = last_state.pl_stage,
            .srcAccessMask = last_state.access_mask,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = last_state.layout,
            .newLayout = dst_layout,
            .image = backing->image,
            .subresourceRange{
                .aspectMask = aspect_mask,
                .baseMipLevel = 0,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .baseArrayLayer = 0,
                .layerCount = VK_REMAINING_ARRAY_LAYERS,
            },
        });
    }

    last_state.layout = dst_layout;
    last_state.access_mask = dst_access;
    last_state.pl_stage = dst_stage;

    return prev_num_barriers != image_barriers.size();
}

void Runtime::SetBackingSamples(VideoCore::Image& image, u32 num_samples, bool copy_backing) {
    auto* const backing = image.backing;
    if (!backing || backing->num_samples == num_samples) {
        return;
    }
    ASSERT_MSG(!image.info.props.is_depth, "Swapping samples is only valid for color images");
    VideoCore::BackingImage* new_backing;
    auto it =
        std::ranges::find(image.backing_images, num_samples, &VideoCore::BackingImage::num_samples);
    if (it == image.backing_images.end()) {
        auto new_image_ci = backing->image.image_ci;
        new_image_ci.samples = LiverpoolToVK::NumSamples(num_samples, image.supported_samples);

        new_backing = &image.backing_images.emplace_back();
        new_backing->num_samples = num_samples;
        new_backing->image = VideoCore::UniqueImage{instance.GetDevice(), instance.GetAllocator()};
        new_backing->image.Create(new_image_ci);

        const auto& info = image.info;
        Vulkan::SetObjectName(instance.GetDevice(), new_backing->image.image,
                              "Image {}x{}x{} {} {} {:#x}:{:#x} L:{} M:{} S:{} (backing)",
                              info.size.width, info.size.height, info.size.depth,
                              AmdGpu::NameOf(info.tile_mode), vk::to_string(info.pixel_format),
                              info.guest_address, info.guest_size, info.resources.layers,
                              info.resources.levels, num_samples);
    } else {
        new_backing = std::addressof(*it);
    }

    if (copy_backing) {
        scheduler.EndRendering();

        const auto resources = image.info.resources;
        ASSERT(resources.levels == 1 && resources.layers == 1);

        constexpr auto dst_stage = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        constexpr auto dst_access = vk::AccessFlagBits2::eColorAttachmentWrite;
        constexpr auto dst_layout = vk::ImageLayout::eColorAttachmentOptimal;

        // Transition current backing to shader read layout
        bool needs_flush = TransitionBackingLayout(
            backing, resources, vk::ImageAspectFlagBits::eColor,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderRead);

        // Transition dest backing to color attachment layout, not caring of previous contents
        needs_flush |=
            TransitionBackingLayout(new_backing, resources, vk::ImageAspectFlagBits::eColor,
                                    dst_layout, dst_stage, dst_access);
        if (needs_flush) {
            FlushBarriers();
        }

        // Copy between ms and non ms backing images
        blit_helper.CopyBetweenMsImages(
            image.info.size.width, image.info.size.height, new_backing->num_samples,
            image.info.pixel_format, backing->num_samples > 1, backing->image, new_backing->image);

        // Update current layout in tracker to new backings layout
        new_backing->state.layout = dst_layout;
        new_backing->state.access_mask = dst_access;
        new_backing->state.pl_stage = dst_stage;
    }

    image.backing = new_backing;
}

void Runtime::AccessBuffer(const VideoCore::Buffer& buffer, u64 offset, u64 size,
                           vk::PipelineStageFlags2 src_stage, vk::AccessFlags2 src_access) {
    const AddressRange range = {
        .resource = std::bit_cast<u64>(buffer.Handle()),
        .range_start = buffer.CpuAddr() + offset,
        .range_end = buffer.CpuAddr() + offset + size - 1,
    };

    if (src_access & AccessWriteMask) {
        barrier_tracker.InsertRange(range, Access::Write);
    }
    if (src_access & AccessReadMask) {
        barrier_tracker.InsertRange(range, Access::Read);
    }

    memory_barrier.srcStageMask |= src_stage;
    memory_barrier.srcAccessMask |= src_access;
}

void Runtime::FlushBarriers() {
    vk::DependencyInfo dep_info;

    if (memory_barrier.srcStageMask) {
        dep_info.pMemoryBarriers = &memory_barrier;
        dep_info.memoryBarrierCount = 1U;
    }
    if (!image_barriers.empty()) {
        dep_info.pImageMemoryBarriers = image_barriers.data();
        dep_info.imageMemoryBarrierCount = static_cast<u32>(image_barriers.size());
    }

    if (dep_info.memoryBarrierCount == 0 && dep_info.imageMemoryBarrierCount == 0) {
        return;
    }

    scheduler.EndRendering();

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.pipelineBarrier2(dep_info);

    memory_barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
    memory_barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
    image_barriers.clear();
}

} // namespace Vulkan
