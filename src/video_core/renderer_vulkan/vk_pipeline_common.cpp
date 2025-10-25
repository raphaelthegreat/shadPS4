// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/static_vector.hpp>

#include "shader_recompiler/resource.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_pipeline_common.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

Pipeline::Pipeline(const Instance& instance_, Scheduler& scheduler_, DescriptorHeap& desc_heap_,
                   const Shader::Profile& profile_, vk::PipelineCache pipeline_cache,
                   bool is_compute_ /*= false*/)
    : instance{instance_}, scheduler{scheduler_}, desc_heap{desc_heap_}, profile{profile_},
      is_compute{is_compute_} {}

Pipeline::~Pipeline() = default;

void Pipeline::BindResources(DescriptorWrites& set_writes, const BufferBarriers& buffer_barriers,
                             const Shader::PushData& push_data) const {
    if (!buffer_barriers.empty()) {
        scheduler.EndRendering();
        scheduler.Record([buffer_barriers](vk::CommandBuffer cmdbuf) {
            const auto dependencies = vk::DependencyInfo{
                .dependencyFlags = vk::DependencyFlagBits::eByRegion,
                .bufferMemoryBarrierCount = u32(buffer_barriers.size()),
                .pBufferMemoryBarriers = buffer_barriers.data(),
            };
            cmdbuf.pipelineBarrier2(dependencies);
        });
    }

    scheduler.Record([is_compute = IsCompute(), uses_push_descriptors = uses_push_descriptors,
                      pipeline_layout = *pipeline_layout, device = instance.GetDevice(),
                      desc_layout = *desc_layout, desc_heap = &desc_heap, push_data,
                      set_writes = std::move(set_writes)](vk::CommandBuffer cmdbuf) mutable {
        const auto bind_point =
            is_compute ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;
        const auto stage_flags =
            is_compute ? vk::ShaderStageFlagBits::eCompute : AllGraphicsStageBits;
        cmdbuf.pushConstants(pipeline_layout, stage_flags, 0u, sizeof(push_data), &push_data);

        if (set_writes.Empty()) {
            return;
        }
        if (uses_push_descriptors) {
            cmdbuf.pushDescriptorSetKHR(bind_point, pipeline_layout, 0, set_writes.writes);
            return;
        }
        const auto desc_set = desc_heap->Commit(desc_layout);
        for (auto& set_write : set_writes.writes) {
            set_write.dstSet = desc_set;
        }
        device.updateDescriptorSets(set_writes.writes, {});
        cmdbuf.bindDescriptorSets(bind_point, pipeline_layout, 0, desc_set, {});
    });
}

std::string Pipeline::GetDebugString() const {
    std::string stage_desc;
    for (const auto& stage : stages) {
        if (stage) {
            const auto shader_name = PipelineCache::GetShaderName(stage->stage, stage->pgm_hash);
            if (stage_desc.empty()) {
                stage_desc = shader_name;
            } else {
                stage_desc = fmt::format("{},{}", stage_desc, shader_name);
            }
        }
    }
    return stage_desc;
}

} // namespace Vulkan
