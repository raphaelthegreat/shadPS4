#pragma once

#include "common/types.h"

namespace Shader::Gcn {

struct GcnShaderInstruction;

class GcnInstructionIterator {
public:
    GcnInstructionIterator();
    ~GcnInstructionIterator();

protected:
    void advanceProgramCounter(const GcnShaderInstruction& ins);
    void resetProgramCounter(u32 pc = 0);

    u32 getBranchTarget(const GcnShaderInstruction& ins);

protected:
    u32 m_programCounter = 0;
};

} // namespace Shader::Gcn
