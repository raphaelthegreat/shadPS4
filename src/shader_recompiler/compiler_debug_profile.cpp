#include "common/assert.h"
#include "shader_recompiler/compiler.h"

namespace Shader::Gcn {

void GcnCompiler::emitDebugProfile(const GcnShaderInstruction& ins) {
    this->emitDbgPro(ins);
}

void GcnCompiler::emitDbgPro(const GcnShaderInstruction& ins) {
    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::S_TTRACEDATA:
        // Nothing to do.
        break;
    default:
        UNREACHABLE();
        break;
    }
}
} // namespace Shader::Gcn
