
#include <fmt/format.h>
#include "common/logging/log.h"
#include "common/io_file.h"
#include "shader_recompiler/analysis.h"
#include "shader_recompiler/compiler.h"
#include "shader_recompiler/control_flow/stackifier.h"
#include "shader_recompiler/decoder.h"
#include "shader_recompiler/module.h"

namespace Shader::Gcn {

GcnModule::GcnModule(const u8* code)
    : m_header(code), m_programInfo(m_header.type()), m_code(code) {}

GcnModule::~GcnModule() = default;

std::vector<u32> GcnModule::compile(const GcnShaderMeta& meta,
                                    const GcnModuleInfo& moduleInfo) const {
    // Decode shader binary
    const u32* start = reinterpret_cast<const u32*>(m_code);
    const u32* end = reinterpret_cast<const u32*>(m_code + m_header.length());
    GcnCodeSlice slice(start, end);
    auto insList = decodeShader(slice);

    // Generate global information
    GcnAnalysisInfo analysisInfo;
    GcnAnalyzer analyzer(m_programInfo, analysisInfo);
    runAnalyzer(analyzer, insList);

    // Do the compile
    GcnCompiler compiler(this->name(), moduleInfo, m_programInfo, m_header, meta, analysisInfo);
    runCompiler(compiler, insList);
    return compiler.finalize();
}

GcnInstructionList GcnModule::decodeShader(GcnCodeSlice& slice) const {
    GcnInstructionList insList;
    GcnDecodeContext decoder;

    // Decode and save instructions
    insList.reserve(m_header.length() / sizeof(u32));
    while (!slice.atEnd()) {
        decoder.decodeInstruction(slice);

        insList.emplace_back(decoder.getInstruction());
    }

    return insList;
}

void GcnModule::runAnalyzer(GcnAnalyzer& analyzer, const GcnInstructionList& insList) const {
    for (auto& ins : insList) {
        analyzer.processInstruction(ins);
    }
}

void GcnModule::runCompiler(GcnCompiler& compiler, const GcnInstructionList& insList) const {
    LOG_DEBUG(Shader_Gcn, "Shader name {}", this->name());
    GcnCfgPass cfgPass;
    auto& cfg = cfgPass.generateCfg(insList);

    dumpShader();
    // auto dot = GcnCfgPass::dumpDot(cfg);
    // std::ofstream fout(fmt::format("shaders/{}.dot", this->name()));
    // fout << dot << std::endl;
    // fout.close();

    GcnStackifier stackifier(cfg);
    const auto tokenList = stackifier.structurize();
    compiler.compile(tokenList);
}

void GcnModule::dumpShader() const {
    const auto dump_name = fmt::format("shaders/{}.bin", this->name());
    Common::FS::IOFile file{dump_name, Common::FS::FileAccessMode::Write};
    file.WriteRaw<u8>(m_code, m_header.length());
}

} // namespace Shader::Gcn
