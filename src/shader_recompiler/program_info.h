#pragma once

#include <string>

#include "common/types.h"
#include "shader_recompiler/spirv/spirv.hpp"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Shader::Gcn {

/**
 * \brief GCN Program type
 *
 * Defines the shader stage that a GCN module has been compiled from.
 */
enum class GcnProgramType : u16 {
    VertexShader = 0,
    PixelShader = 1,
    ComputeShader = 3,
    GeometryShader = 4,
    HullShader = 5,
    DomainShader = 6,
};

/**
 * \brief Convert VkPipelineStageFlags to GcnProgramType
 */
GcnProgramType gcnProgramTypeFromVkStage(vk::PipelineStageFlagBits stage);

/**
 * \brief DXBC shader info
 *
 * Stores the shader program type.
 */
class GcnProgramInfo {
public:
    GcnProgramInfo() = default;
    GcnProgramInfo(GcnProgramType type) : m_type(type) {}

    /**
     * \brief Program type
     * \returns Program type
     */
    GcnProgramType type() const {
        return m_type;
    }

    /**
     * \brief Shader stage name
     * \returns Shader stage name
     */
    std::string name() const;

    /**
     * \brief Vulkan shader stage
     *
     * The \c VkShaderStageFlagBits constant that corresponds to the program type.
     * \returns Vulkan shaer stage
     */
    vk::ShaderStageFlagBits shaderStage() const;

    /**
     * \brief SPIR-V execution model
     *
     * The execution model that corresponds to the Vulkan shader stage.
     * \returns SPIR-V execution model
     */
    spv::ExecutionModel executionModel() const;

private:
    GcnProgramType m_type = GcnProgramType::PixelShader;
};

} // namespace Shader::Gcn
