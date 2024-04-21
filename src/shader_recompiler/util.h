#pragma once

#include "common/types.h"
#include "shader_recompiler/compiler_defs.h"
#include "shader_recompiler/program_info.h"

namespace Shader::Gcn {

/**
 * \brief Binding numbers and properties
 */
enum DxbcBindingProperties : u32 {
    GcnConstBufferBindingIndex = 0,
    GcnConstBufferBindingCount = GcnMaxSGPR,

    GcnSamplerBindingIndex = GcnConstBufferBindingIndex + GcnConstBufferBindingCount,
    GcnSamplerBindingCount = GcnMaxSGPR,

    GcnResourceBindingIndex = GcnSamplerBindingIndex + GcnSamplerBindingCount,
    GcnResourceBindingCount = GcnMaxSGPR,

    GcnStageBindingCount =
        GcnConstBufferBindingCount + GcnSamplerBindingCount + GcnResourceBindingCount,
};

/**
 * \brief Computes first binding index for a given stage
 *
 * \param [in] stage The shader stage
 * \returns Index of first binding
 */
inline u32 computeStageBindingOffset(GcnProgramType stage) {
    return GcnStageBindingCount * static_cast<u32>(stage);
}

/**
 * \brief Computes constant buffer binding index
 *
 * Including ImmResource and ImmConstBuffer
 *
 * \param [in] stage Shader stage
 * \param [in] index Start register index
 * \returns Binding index
 */
inline u32 computeConstantBufferBinding(GcnProgramType stage, u32 index) {
    return computeStageBindingOffset(stage) + GcnConstBufferBindingIndex + index;
}

/**
 * \brief Computes buffer/texture resource binding index
 *
 * \param [in] stage Shader stage
 * \param [in] index Start register index
 * \returns Binding index
 */
inline u32 computeResourceBinding(GcnProgramType stage, u32 index) {
    return computeStageBindingOffset(stage) + GcnResourceBindingIndex + index;
}

/**
 * \brief Computes sampler binding index
 *
 * \param [in] stage Shader stage
 * \param [in] index Start register index
 * \returns Binding index
 */
inline u32 computeSamplerBinding(GcnProgramType stage, u32 index) {
    return computeStageBindingOffset(stage) + GcnSamplerBindingIndex + index;
}
} // namespace Shader::Gcn
