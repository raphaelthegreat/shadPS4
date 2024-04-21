#pragma once

#include "common/types.h"
#include "shader_recompiler/decoder.h"
#include "shader_recompiler/header.h"
#include "shader_recompiler/program_info.h"

namespace Shader::Gcn {

union GcnShaderMeta;
class GcnAnalyzer;
class GcnCompiler;
struct GcnModuleInfo;

class GcnModule {
public:
    GcnModule(const u8* code);
    ~GcnModule();

    /**
     * \brief Shader type
     * \returns Shader type
     */
    const GcnProgramInfo& programInfo() const {
        return m_programInfo;
    }

    /**
     * \brief Get resources bound to the shader
     */
    const GcnShaderResourceTable& getResourceTable() const {
        return m_header.getShaderResourceTable();
    }

    /**
     * \brief Get shader name
     *
     * The name is generate from original
     * GCN shader binary hash and crc.
     */
    std::string name() const {
        return m_programInfo.name() + m_header.key().name();
    }

    /**
     * \brief Compiles GCN shader to SPIR-V module
     *
     * \returns The SPIR-V module code.
     */
    std::vector<u32> compile(const GcnShaderMeta& meta, const GcnModuleInfo& moduleInfo) const;

private:
    GcnInstructionList decodeShader(GcnCodeSlice& slice) const;

    void runAnalyzer(GcnAnalyzer& analyzer, const GcnInstructionList& insList) const;

    void runCompiler(GcnCompiler& compiler, const GcnInstructionList& insList) const;

    void dumpShader() const;

private:
    GcnHeader m_header;
    GcnProgramInfo m_programInfo;
    const u8* m_code;

    GcnShaderResourceTable m_resourceTable;
};

} // namespace Shader::Gcn
