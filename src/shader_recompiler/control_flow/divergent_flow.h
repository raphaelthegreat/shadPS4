#pragma once

#include "shader_recompiler/control_flow/token_list.h"

namespace Shader::Gcn {

class GcnDivergentFlow {
public:
    GcnDivergentFlow(GcnTokenFactory& factory);
    ~GcnDivergentFlow();

    void diverge(GcnTokenList& tokens);

private:
    bool needDiverge(GcnToken* token);
    void divergeCode(GcnToken* token);

    GcnDivergentAction getDivergentAction(const GcnShaderInstruction& ins);

    GcnShaderInstruction makeClearInstruction();

    bool isNonCompileInst(const GcnShaderInstruction& ins);

private:
    GcnTokenFactory& m_factory;
    GcnTokenList* m_tokens;
};
} // namespace Shader::Gcn
