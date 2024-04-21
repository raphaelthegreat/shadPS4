#pragma once

#include "shader_recompiler/shader_binary.h"

namespace Shader::Gcn {

class GcnFetchShader {
public:
    GcnFetchShader(const u8* code);
    ~GcnFetchShader();

    const VertexInputSemanticTable& getVertexInputSemanticTable() const {
        return m_vsInputSemanticTable;
    }

private:
    void parseVsInputSemantic(const u8* code);

private:
    VertexInputSemanticTable m_vsInputSemanticTable;
};

} // namespace Shader::Gcn
