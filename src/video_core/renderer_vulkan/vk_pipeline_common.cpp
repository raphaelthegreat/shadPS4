// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/container/static_vector.hpp>

#include "shader_recompiler/resource.h"
#include "video_core/renderer_vulkan/vk_descriptor_ring.h"
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

void Pipeline::BindResources(std::span<const Descriptor> descriptors,
                             const Shader::PushData& push_data) const {
    const auto bind_point =
        IsCompute() ? vk::PipelineBindPoint::eCompute : vk::PipelineBindPoint::eGraphics;
    const auto stage_flags = IsCompute() ? vk::ShaderStageFlagBits::eCompute : AllGraphicsStageBits;
    scheduler.Record([device = instance.GetDevice(), &desc_heap = desc_heap,
                      layout = *pipeline_layout, set_layout = *desc_layout,
                      push = uses_push_descriptors, bind_point, stage_flags, descriptors,
                      push_data](vk::CommandBuffer cmdbuf) {
        cmdbuf.pushConstants(layout, stage_flags, 0u, sizeof(push_data), &push_data);
        if (descriptors.empty()) {
            return;
        }

        thread_local std::vector<vk::WriteDescriptorSet> writes;
        writes.clear();
        u32 array_element = 0;
        for (size_t i = 0; i < descriptors.size(); ++i) {
            const auto& descriptor = descriptors[i];
            array_element =
                i > 0 && descriptors[i - 1].binding == descriptor.binding ? array_element + 1 : 0;
            const bool is_buffer = descriptor.type == vk::DescriptorType::eStorageBuffer;
            writes.push_back(vk::WriteDescriptorSet{
                .dstBinding = descriptor.binding,
                .dstArrayElement = array_element,
                .descriptorCount = 1,
                .descriptorType = descriptor.type,
                .pImageInfo =
                    is_buffer ? nullptr
                              : reinterpret_cast<const vk::DescriptorImageInfo*>(&descriptor.image),
                .pBufferInfo = is_buffer ? reinterpret_cast<const vk::DescriptorBufferInfo*>(
                                               &descriptor.buffer)
                                         : nullptr,
            });
        }

        if (push) {
            cmdbuf.pushDescriptorSetKHR(bind_point, layout, 0, writes);
            return;
        }
        const auto desc_set = desc_heap.Commit(set_layout);
        for (auto& write : writes) {
            write.dstSet = desc_set;
        }
        device.updateDescriptorSets(writes, {});
        cmdbuf.bindDescriptorSets(bind_point, layout, 0, desc_set, {});
    });
}

std::string Pipeline::GetDebugString() const {
    std::string stage_desc;
    for (const auto& stage : stages) {
        if (stage) {
            const auto shader_name = PipelineCache::GetShaderName(stage->hw_stage, stage->pgm_hash);
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
