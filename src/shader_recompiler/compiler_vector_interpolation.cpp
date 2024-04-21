#include "common/assert.h"
#include "shader_recompiler/compiler.h"
#include "shader_recompiler/decoder.h"

namespace Shader::Gcn {

void GcnCompiler::emitVectorInterpolation(const GcnShaderInstruction& ins) {
    this->emitVectorInterpFpCache(ins);
}

void GcnCompiler::emitVectorInterpFpCache(const GcnShaderInstruction& ins) {
    auto vinterp = gcnInstructionAs<GcnShaderInstVINTRP>(ins);

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::V_INTERP_P1_F32:
        // Skip the first interpolation step, let the GPU do it automatically.
        break;
    case GcnOpcode::V_INTERP_P2_F32: {
        ASSERT_MSG(m_programInfo.type() == GcnProgramType::PixelShader,
                   "TODO: support vinterp for non-pixel shaders.");
        // Stores the auto-interpolated value to dst vgpr.
        u32 attrIdx = vinterp.control.attr;
        u32 regIdx = m_meta.ps.semanticMapping[attrIdx].m_outIndex;
        auto mask = GcnRegMask::select(vinterp.control.chan);

        auto& input = m_inputs[regIdx];
        auto compPtr = emitVectorAccess(input, spv::StorageClass::StorageClassInput, mask);
        auto value = emitValueLoad(compPtr);
        emitVgprStore(vinterp.vdst, value);
        break;
    }
    default:
        UNIMPLEMENTED();
        break;
    }
}
} // namespace Shader::Gcn
