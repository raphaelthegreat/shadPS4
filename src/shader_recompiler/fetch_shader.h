#pragma once

#include "shader_recompiler/shader_binary.h"

namespace Shader::Gcn::FetchShader {

/**
 * @brief ParseVsInputSemantic
 *
 * Parses the fetch shader instruction block and extracts the sematic mappings
 * from the shader instructions.
 */
VertexInputSemanticTable ParseInputSemantic(const u64 code_ptr);

} // namespace Shader::Gcn
