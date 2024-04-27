#pragma once

#include <variant>
#include "shader_recompiler/instruction.h"

namespace Shader::Gcn {

class GcnCodeSlice {
public:
    GcnCodeSlice(const u32* ptr, const u32* end) : m_ptr(ptr), m_end(end) {}
    GcnCodeSlice(const GcnCodeSlice& other) = default;
    ~GcnCodeSlice() = default;

    u32 at(u32 id) const {
        return m_ptr[id];
    }

    u32 readu32() {
        return *(m_ptr++);
    }

    u64 readu64() {
        u64 value = *(u64*)m_ptr;
        m_ptr += 2;
        return value;
    }

    bool atEnd() const {
        return m_ptr == m_end;
    }

public:
    const u32* m_ptr = nullptr;
    const u32* m_end = nullptr;
};

/**
 * \brief GCN instruction decoder
 */
class GcnDecodeContext {
    enum GcnOperandFieldRange {
        ScalarGPRMin = 0,
        ScalarGPRMax = 103,
        SignedConstIntPosMin = 129,
        SignedConstIntPosMax = 192,
        SignedConstIntNegMin = 193,
        SignedConstIntNegMax = 208,
        VectorGPRMin = 256,
        VectorGPRMax = 511
    };

public:
    GcnDecodeContext();
    ~GcnDecodeContext();

    const GcnShaderInstruction& getInstruction() const {
        return m_instruction;
    }

    void decodeInstruction(GcnCodeSlice& code);

private:
    GcnInstEncoding getInstructionEncoding(u32 token);
    u32 getEncodingLength(GcnInstEncoding encoding);
    u32 getOpMapOffset(GcnInstEncoding encoding);
    u32 mapEncodingOp(GcnInstEncoding encoding, GcnOpcode opcode);
    bool hasAdditionalLiteral(GcnInstEncoding encoding, u32 opcode);
    void updateInstructionMeta(GcnInstEncoding encoding);
    u32 getMimgModifier(GcnOpcode opcode);
    void repairOperandType();

    GcnOperandField getOperandField(u32 code);

    void decodeInstruction32(GcnInstEncoding encoding, GcnCodeSlice& code);
    void decodeInstruction64(GcnInstEncoding encoding, GcnCodeSlice& code);
    void decodeLiteralConstant(GcnInstEncoding encoding, GcnCodeSlice& code);

    // 32 bits encodings
    void decodeInstructionSOP1(u32 hexInstruction);
    void decodeInstructionSOPP(u32 hexInstruction);
    void decodeInstructionSOPC(u32 hexInstruction);
    void decodeInstructionSOPK(u32 hexInstruction);
    void decodeInstructionSOP2(u32 hexInstruction);
    void decodeInstructionVOP1(u32 hexInstruction);
    void decodeInstructionVOPC(u32 hexInstruction);
    void decodeInstructionVOP2(u32 hexInstruction);
    void decodeInstructionSMRD(u32 hexInstruction);
    void decodeInstructionVINTRP(u32 hexInstruction);

    // 64 bits encodings
    void decodeInstructionVOP3(uint64_t hexInstruction);
    void decodeInstructionMUBUF(uint64_t hexInstruction);
    void decodeInstructionMTBUF(uint64_t hexInstruction);
    void decodeInstructionMIMG(uint64_t hexInstruction);
    void decodeInstructionDS(uint64_t hexInstruction);
    void decodeInstructionEXP(uint64_t hexInstruction);

private:
    GcnShaderInstruction m_instruction;
};

/**
 * \brief Convenient function to convert general instruction into specific encodings
 */
using GcnInstructionVariant =
    std::variant<GcnShaderInstSOP1, GcnShaderInstSOP2, GcnShaderInstSOPK, GcnShaderInstSOPC,
                 GcnShaderInstSOPP, GcnShaderInstVOP1, GcnShaderInstVOP2, GcnShaderInstVOP3,
                 GcnShaderInstVOPC, GcnShaderInstSMRD, GcnShaderInstMUBUF, GcnShaderInstMTBUF,
                 GcnShaderInstMIMG, GcnShaderInstVINTRP, GcnShaderInstDS, GcnShaderInstEXP>;

// Do not use this directly, use gcnInstructionAs
GcnInstructionVariant gcnInstructionConvert(const GcnShaderInstruction& ins);

template <typename InsType>
static inline const InsType gcnInstructionAs(const GcnShaderInstruction& ins) {
    return std::get<InsType>(gcnInstructionConvert(ins));
}

} // namespace Shader::Gcn
