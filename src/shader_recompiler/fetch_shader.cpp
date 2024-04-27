#include <deque>
#include "common/assert.h"
#include "common/io_file.h"
#include "shader_recompiler/decoder.h"
#include "shader_recompiler/fetch_shader.h"

namespace Shader::Gcn::FetchShader {

VertexInputSemanticTable ParseInputSemantic(const u64 code_ptr) {
    const u32* start = reinterpret_cast<const u32*>(code_ptr);
    const u32* end = reinterpret_cast<const u32*>(code_ptr + std::numeric_limits<u32>::max());

    VertexInputSemanticTable sema_table;
    GcnCodeSlice codeSlice(start, end);
    GcnDecodeContext decoder;

    struct VsharpLoad {
        u32 index;
        u32 dst_sgpr;
    };
    std::deque<VsharpLoad> loads;

    u32 semantic_index = 0;
    while (!codeSlice.atEnd()) {
        decoder.decodeInstruction(codeSlice);

        // s_load_dwordx4 s[8:11], s[2:3], 0x00                      // 00000000: C0840300
        // s_load_dwordx4 s[12:15], s[2:3], 0x04                     // 00000004: C0860304
        // s_load_dwordx4 s[16:19], s[2:3], 0x08                     // 00000008: C0880308
        // s_waitcnt     lgkmcnt(0)                                  // 0000000C: BF8C007F
        // buffer_load_format_xyzw v[4:7], v0, s[8:11], 0 idxen      // 00000010: E00C2000 80020400
        // buffer_load_format_xyz v[8:10], v0, s[12:15], 0 idxen     // 00000018: E0082000 80030800
        // buffer_load_format_xy v[12:13], v0, s[16:19], 0 idxen     // 00000020: E0042000 80040C00
        // s_waitcnt     0                                           // 00000028: BF8C0000
        // s_setpc_b64   s[0:1]                                      // 0000002C: BE802000

        // s_load_dwordx4  s[4:7], s[2:3], 0x0
        // s_waitcnt       lgkmcnt(0)
        // buffer_load_format_xyzw v[4:7], v0, s[4:7], 0 idxen
        // s_load_dwordx4  s[4:7], s[2:3], 0x8
        // s_waitcnt       lgkmcnt(0)
        // buffer_load_format_xyzw v[8:11], v0, s[4:7], 0 idxen
        // s_waitcnt       vmcnt(0) & expcnt(0) & lgkmcnt(0)
        // s_setpc_b64     s[0:1]

        // A normal fetch shader looks like the above, the instructions are generated
        // using input semantics on cpu side. Load instructions can either be separate or interleaved
        // We take the reverse way, extract the original input semantics from these instructions.

        const auto& ins = decoder.getInstruction();
        if (ins.opcode == GcnOpcode::S_SETPC_B64) {
            break;
        }

        if (ins.opClass == GcnInstClass::ScalarMemRd) {
            GcnShaderInstSMRD smrd = gcnInstructionAs<GcnShaderInstSMRD>(ins);
            loads.emplace_back(u32(smrd.control.offset) >> 2U, smrd.sdst.code);
            continue;
        }

        if (ins.opClass == GcnInstClass::VectorMemBufFmt) {
            GcnShaderInstMUBUF mubuf = gcnInstructionAs<GcnShaderInstMUBUF>(ins);

            // Find the load instruction that loaded the V# to the SPGR.
            // This is so we can determine its index in the vertex table.
            const auto it = std::ranges::find_if(loads, [&](VsharpLoad& load) {
                return load.dst_sgpr == mubuf.srsrc.code << 2;
            });

            auto& semantic = sema_table.emplace_back();
            semantic.semantic = semantic_index++;
            semantic.vsharp_index = it->index;
            semantic.dest_vgpr = mubuf.vdata.code;
            semantic.num_elements = mubuf.control.count;
            loads.erase(it);
        }
    }

    // const std::string_view path = "fetch.bin";
    // Common::FS::IOFile file{path, Common::FS::FileAccessMode::Write};
    // file.WriteRaw<u32>((const u32*)code_ptr, codeSlice.m_ptr - (const u32*)code_ptr);
    // file.Close();

    return sema_table;
}

} // namespace Shader::Gcn::FetchShader
