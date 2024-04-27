// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/io_file.h"
#include "shader_recompiler/fetch_shader.h"
#include "shader_recompiler/gcn_constants.h"
#include "shader_recompiler/util.h"
#include "shader_recompiler/header.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/sharp_buffer.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/memory_manager.h"

namespace Vulkan {

static u32 findUsageRegister(const ShaderResourceTable& table, ShaderInputUsageType usage) {
    const auto it = std::ranges::find_if(
        table, [usage](const ShaderResource& res) { return res.usage == usage; });
    if (it != table.end()) {
        return it->startRegister;
    }
    return -1;
}

PipelineCache::PipelineCache(const Instance& instance_, Scheduler& scheduler_,
                             VideoCore::MemoryManager& memory_manager_, AmdGpu::Liverpool* liverpool_)
    : instance{instance_}, scheduler{scheduler_}, memory_manager{memory_manager_},
      liverpool{liverpool_} {
    pipeline_cache = instance.GetDevice().createPipelineCacheUnique({});
    const vk::DescriptorSetLayoutBinding binding = {
        .binding = 4,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eVertex,
        .pImmutableSamplers = nullptr,
    };
    auto desc_layout = instance.GetDevice().createDescriptorSetLayout(vk::DescriptorSetLayoutCreateInfo{
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR,
        .bindingCount = 1U,
        .pBindings = &binding,
    });
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 1U,
        .pSetLayouts = &desc_layout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges = nullptr,
    };
    pipeline_layout = instance.GetDevice().createPipelineLayoutUnique(layout_info);
}

PipelineCache::~PipelineCache() = default;

void PipelineCache::UpdateVertexStage() {
    auto& vs_program = liverpool->regs.vs_program;
    if (!vs_program.Address()) {
        return;
    }

    // Lookup gcn shader module.
    const u8* code = vs_program.Address();
    const auto key = BinaryInfo(code).key();
    const auto& [it, new_module] = modules.try_emplace(key, code);
    UpdateVertexBinding(it->second);

    // Create and bind shader resources
    auto res_table = it->second.getResourceTable();
    BindResources(vk::PipelineStageFlagBits::eVertexShader, res_table, vs_program.user_data);

    // Compile the shader
    vs_code = it->second.compile(metas[0], module_info);

    Common::FS::IOFile file(std::string_view("vs_code.spv"), Common::FS::FileAccessMode::Write);
    file.Write(vs_code);
    file.Close();

    Common::FS::IOFile infile(std::string_view("vs_code.spv"), Common::FS::FileAccessMode::Read);
    std::vector<u8> buf(infile.GetSize());
    infile.Read(buf);
    infile.Close();

    ASSERT(vs_code.size() * sizeof(u32) == buf.size());
    ASSERT(std::memcmp(vs_code.data(), buf.data(), buf.size()) == 0);
}

void PipelineCache::UpdatePixelStage() {
    auto& ps_program = liverpool->regs.ps_program;
    if (!ps_program.Address()) {
        return;
    }

    // Lookup gcn shader module.
    const u8* code = ps_program.Address();
    const auto key = BinaryInfo(code).key();
    const auto& [it, new_module] = modules.try_emplace(key, code);

    // Create and bind shader resources
    auto res_table = it->second.getResourceTable();
    BindResources(vk::PipelineStageFlagBits::eFragmentShader, res_table, ps_program.user_data);

    // Compile the shader
    metas[1].ps.inputSemanticCount = 1;
    ps_code = it->second.compile(metas[1], module_info);

    Common::FS::IOFile file(std::string_view("ps_code.spv"), Common::FS::FileAccessMode::Write);
    file.Write(vs_code);
    file.Close();
}

void PipelineCache::BindPipeline() {
    UpdateVertexStage();
    UpdatePixelStage();

    if (!pipeline) {
        this->key.prim_type = liverpool->regs.primitive_type;
        this->key.polygon_mode = liverpool->regs.polygon_control.PolyMode();
        pipeline = std::make_unique<GraphicsPipeline>(instance, this->key, *pipeline_cache,
                                                      *pipeline_layout, vs_code, ps_code);
    }
    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline->Handle());
}

void PipelineCache::UpdateVertexBinding(const GcnModule& module) {
    auto& vs_program = liverpool->regs.vs_program;
    auto res_table = module.getResourceTable();

    // Search if user data regs contain a pointer to the fetch shader.
    const u32 fs_reg = findUsageRegister(res_table, ShaderInputUsageType::SubPtrFetchShader);
    if (fs_reg == -1) {
        key.num_attributes = 0;
        key.num_bindings = 0;
        return;
    }

    // Fetch the table.
    u64 fs_code;
    std::memcpy(&fs_code, &vs_program.user_data[fs_reg], sizeof(u64));
    sema_table = FetchShader::ParseInputSemantic(fs_code);

    // Nothing more to do if the semantic table is empty.
    if (sema_table.empty()) {
        key.num_attributes = 0;
        key.num_bindings = 0;
        return;
    }

    // Extract vertex table.
    const u32 vt_reg = findUsageRegister(res_table, ShaderInputUsageType::PtrVertexBufferTable);
    ASSERT_MSG(vt_reg >= 0, "Vertex table not found while input semantic exist.");

    const u32* vertex_table;
    std::memcpy(&vertex_table, &vs_program.user_data[vt_reg], sizeof(vertex_table));

    const u32 bindingCount = sema_table.size();
    UpdateInputLayout(vertex_table, bindingCount);
    BindHostBuffers(false, vertex_table, bindingCount);

    // Record shader meta info
    std::ranges::copy(sema_table, metas[0].vs.inputSemanticTable.begin());
    metas[0].vs.inputSemanticCount = sema_table.size();
}

void PipelineCache::UpdateInputLayout(const u32* vertex_table, u32 num_buffers) {
    key.num_attributes = sema_table.size();
    key.num_bindings = key.num_attributes;

    size_t first_attrib_offset = 0;
    for (u32 i = 0; i < key.num_attributes; ++i) {
        auto& semantic = sema_table[i];
        ASSERT_MSG(semantic.semantic == i, "Semantic index is not equal to table index.");
        const u32 offset = semantic.vsharp_index * ShaderConstantDwordSize::kDwordSizeVertexBuffer;

        AmdGpu::Buffer vsharp;
        std::memcpy(&vsharp, vertex_table + offset, sizeof(vsharp));

        if (first_attrib_offset == 0) {
            first_attrib_offset = vsharp.base_address;
        }

        // Attributes
        key.attributes[i].location = semantic.semantic;
        key.attributes[i].binding = semantic.semantic;
        key.attributes[i].format = LiverpoolToVK::SurfaceFormat(vsharp.data_format, vsharp.num_format);
        key.attributes[i].offset = 0;

        // Bindings
        key.bindings[i].binding = semantic.semantic;
        key.bindings[i].stride = vsharp.stride;
        key.bindings[i].inputRate = vk::VertexInputRate::eVertex;

        // Fix element count
        const u32 num_components = AmdGpu::getNumComponents(vsharp.data_format);
        semantic.num_elements = std::min<u32>(semantic.num_elements, num_components);
    }
}

void PipelineCache::BindHostBuffers(bool is_indexed, const u32* vertex_table, u32 num_buffers) {
    std::array<vk::Buffer, MaxVertexBufferCount> buffers;
    std::array<vk::DeviceSize, MaxVertexBufferCount> offsets;

    // Bind vertex buffers.
    for (u32 i = 0; i < num_buffers; ++i) {
        const auto& semantic = sema_table[i];
        const u32 offset = semantic.vsharp_index * ShaderConstantDwordSize::kDwordSizeVertexBuffer;

        AmdGpu::Buffer vsharp;
        std::memcpy(&vsharp, vertex_table + offset, sizeof(vsharp));

        std::tie(buffers[i], offsets[i]) = memory_manager.GetBufferForRange(vsharp.base_address);
    }

    const auto cmdbuf = scheduler.CommandBuffer();
    cmdbuf.bindVertexBuffers(0, num_buffers, buffers.data(), offsets.data());

    if (!is_indexed) {
        return;
    }

    // Bind index buffers.
}

template <typename T>
const T* findUserData(const ShaderResource& res, u32 eudIndex, const Liverpool::UserData& userData) {
    const T* registerData = nullptr;
    if (!res.inEud) {
        registerData = reinterpret_cast<const T*>(&userData[res.startRegister]);
    } else {
        const uintptr_t eud_ptr = *reinterpret_cast<const uintptr_t*>(&userData[eudIndex]);
        const u32* eudTable = reinterpret_cast<const u32*>(eud_ptr);
        registerData = reinterpret_cast<const T*>(&eudTable[res.eudOffsetInDwords]);
    }
    return registerData;
}

void PipelineCache::BindResources(vk::PipelineStageFlags stage, ShaderResourceTable& table,
                                  const Liverpool::UserData& user_data) {
    const u32 eudIndex = findUsageRegister(table, ShaderInputUsageType::PtrExtendedUserData);
    const auto cmdbuf = scheduler.CommandBuffer();
    for (const auto& res : table) {
        switch (res.type) {
        case vk::DescriptorType::eUniformBuffer: {
            const auto* vsharp = findUserData<AmdGpu::Buffer>(res, eudIndex, user_data);
            const auto [buffer, offset] = memory_manager.GetBufferForRange(vsharp->base_address);
            const u32 slot = computeConstantBufferBinding(GcnProgramType::VertexShader, res.startRegister);

            const vk::DescriptorBufferInfo buf_info = {
                .buffer = buffer,
                .offset = offset,
                .range = 65536,
            };
            const vk::WriteDescriptorSet write_set = {
                .dstSet = VK_NULL_HANDLE,
                .dstBinding = 4,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .pBufferInfo = &buf_info,
            };
            cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eGraphics, *pipeline_layout, 0, write_set);

            static const uint32_t indexStrideTable[] = { 8, 16, 32, 64 };
            static const uint32_t elementSizeTable[] = { 2, 4, 8, 16 };

            GcnBufferMeta meta;
            meta.stride = vsharp->stride;
            meta.numRecords = vsharp->num_records;
            meta.dfmt = std::bit_cast<BufferFormat>(vsharp->data_format.Value());
            meta.nfmt = std::bit_cast<BufferChannelType>(vsharp->num_format.Value());
            meta.isSwizzle = vsharp->swizzle_enable;
            meta.indexStride = indexStrideTable[vsharp->index_stride];
            meta.elementSize = elementSizeTable[vsharp->element_size];
            metas[0].vs.bufferInfos[res.startRegister] = meta;
            break;
        }
        }
    }
}

} // namespace Vulkan
