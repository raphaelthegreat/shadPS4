#include "common/assert.h"
#include "shader_recompiler/program_info.h"

namespace Shader::Gcn {

vk::ShaderStageFlagBits GcnProgramInfo::shaderStage() const {
    switch (m_type) {
    case GcnProgramType::PixelShader:
        return vk::ShaderStageFlagBits::eFragment;
    case GcnProgramType::VertexShader:
        return vk::ShaderStageFlagBits::eVertex;
    case GcnProgramType::GeometryShader:
        return vk::ShaderStageFlagBits::eGeometry;
    case GcnProgramType::HullShader:
        return vk::ShaderStageFlagBits::eTessellationControl;
    case GcnProgramType::DomainShader:
        return vk::ShaderStageFlagBits::eTessellationEvaluation;
    case GcnProgramType::ComputeShader:
        return vk::ShaderStageFlagBits::eCompute;
    default:
        UNREACHABLE();
    }
}

spv::ExecutionModel GcnProgramInfo::executionModel() const {
    switch (m_type) {
    case GcnProgramType::PixelShader:
        return spv::ExecutionModelFragment;
    case GcnProgramType::VertexShader:
        return spv::ExecutionModelVertex;
    case GcnProgramType::GeometryShader:
        return spv::ExecutionModelGeometry;
    case GcnProgramType::HullShader:
        return spv::ExecutionModelTessellationControl;
    case GcnProgramType::DomainShader:
        return spv::ExecutionModelTessellationEvaluation;
    case GcnProgramType::ComputeShader:
        return spv::ExecutionModelGLCompute;
    default:
        UNREACHABLE();
    }
}

std::string GcnProgramInfo::name() const {
    switch (m_type) {
    case GcnProgramType::PixelShader:
        return "PS";
    case GcnProgramType::VertexShader:
        return "VS";
    case GcnProgramType::GeometryShader:
        return "GS";
    case GcnProgramType::HullShader:
        return "HS";
    case GcnProgramType::DomainShader:
        return "DS";
    case GcnProgramType::ComputeShader:
        return "CS";
    }
}

GcnProgramType gcnProgramTypeFromVkStage(vk::PipelineStageFlagBits stage) {
    GcnProgramType type = GcnProgramType::VertexShader;
    switch (stage) {
    case vk::PipelineStageFlagBits::eVertexShader:
        type = GcnProgramType::VertexShader;
        break;
    case vk::PipelineStageFlagBits::eFragmentShader:
        type = GcnProgramType::PixelShader;
        break;
    case vk::PipelineStageFlagBits::eComputeShader:
        type = GcnProgramType::ComputeShader;
        break;
    case vk::PipelineStageFlagBits::eGeometryShader:
        type = GcnProgramType::GeometryShader;
        break;
    case vk::PipelineStageFlagBits::eTessellationControlShader:
        type = GcnProgramType::DomainShader;
        break;
    case vk::PipelineStageFlagBits::eTessellationEvaluationShader:
        type = GcnProgramType::HullShader;
        break;
    default:
        UNREACHABLE_MSG("Error pipeline stage.");
        break;
    }
    return type;
}

} // namespace Shader::Gcn
