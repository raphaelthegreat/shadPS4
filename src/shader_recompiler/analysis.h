#pragma once

#include <array>
#include "common/types.h"
#include "shader_recompiler/compiler_defs.h"

namespace Shader::Gcn {

class GcnProgramInfo;
struct GcnShaderInstruction;

struct GcnExportInfo {
    u32 paramCount = 0;
    std::array<GcnRegMask, GcnMaxExportParam> params;
    u32 mrtCount = 0;
    std::array<GcnRegMask, GcnMaxExportParam> mrts;
};

struct GcnAnalysisInfo {
    GcnExportInfo exportInfo;

    bool hasComputeLane = false;
};

/**
 * \brief GCN shader analyzer
 *
 * Pre-collect global information of a shader
 * which is not possible to get when stepping a instruction.
 * The information will later be used by the actual compiler.
 */
class GcnAnalyzer {
public:
    GcnAnalyzer(const GcnProgramInfo& programInfo, GcnAnalysisInfo& analysis);
    virtual ~GcnAnalyzer();

    /**
     * \brief Processes a single instruction
     * \param [in] ins The instruction
     */
    void processInstruction(const GcnShaderInstruction& ins);

private:
    void analyzeInstruction(const GcnShaderInstruction& ins);

    void analyzeExp(const GcnShaderInstruction& ins);

    void analyzeLane(const GcnShaderInstruction& ins);

private:
    GcnAnalysisInfo* m_analysis;
    const GcnProgramInfo& m_programInfo;
};

} // namespace Shader::Gcn
