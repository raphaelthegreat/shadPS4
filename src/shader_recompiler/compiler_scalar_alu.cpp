#include "common/assert.h"
#include "shader_recompiler/compiler.h"

namespace Shader::Gcn {

void GcnCompiler::emitScalarALU(const GcnShaderInstruction& ins) {
    auto opClass = ins.opClass;
    switch (opClass) {
    case GcnInstClass::ScalarArith:
    case GcnInstClass::ScalarMov:
    case GcnInstClass::ScalarBitLogic:
    case GcnInstClass::ScalarBitManip:
    case GcnInstClass::ScalarBitField:
        this->emitScalarAluCommon(ins);
        break;
    case GcnInstClass::ScalarMovRel:
        this->emitScalarMovRel(ins);
        break;
    case GcnInstClass::ScalarAbs:
        this->emitScalarAbs(ins);
        break;
    case GcnInstClass::ScalarCmp:
        this->emitScalarCmp(ins);
        break;
    case GcnInstClass::ScalarSelect:
        this->emitScalarSelect(ins);
        break;
    case GcnInstClass::ScalarConv:
        this->emitScalarConv(ins);
        break;
    case GcnInstClass::ScalarExecMask:
        this->emitScalarExecMask(ins);
        break;
    case GcnInstClass::ScalarQuadMask:
        this->emitScalarQuadMask(ins);
        break;
    default:
        UNREACHABLE();
    }
}

void GcnCompiler::emitScalarAluCommon(const GcnShaderInstruction& ins) {
    std::array<GcnRegisterValuePair, GcnMaxOperandCount> src;
    for (u32 i = 0; i != ins.srcCount; ++i) {
        src[i] = emitRegisterLoad(ins.src[i]);
    }

    GcnRegisterValuePair dst = {};
    dst.low.type.ctype = getDestinationType(ins.dst[0].type);
    dst.low.type.ccount = 1;
    dst.high.type = dst.low.type;

    bool ignoreScc = false;
    const u32 typeId = getVectorTypeId(dst.low.type);

    auto op = ins.opcode;
    switch (op) {
    // ScalarArith
    case GcnOpcode::S_ADD_U32:
    case GcnOpcode::S_ADD_I32: {
        dst.low.id = m_module.opIAdd(typeId, src[0].low.id, src[1].low.id);
        ignoreScc = true;
        break;
    }
    case GcnOpcode::S_ADDK_I32: {
        u32 imm = m_module.consti32(static_cast<int32_t>(ins.control.sopk.simm));
        auto old = emitRegisterLoad(ins.dst[0]);
        dst.low.id = m_module.opIAdd(typeId, old.low.id, imm);
        ignoreScc = true;
        break;
    }
    case GcnOpcode::S_MUL_I32: {
        dst.low.id = m_module.opIMul(typeId, src[0].low.id, src[1].low.id);
        ignoreScc = true;
        break;
    }
    // ScalarMov
    case GcnOpcode::S_MOV_B32: {
        dst.low.id = src[0].low.id;
        ignoreScc = true;
        break;
    }
    case GcnOpcode::S_MOV_B64: {
        // Fix the type from Uint64 to Uint32 at the same time.
        dst.low = src[0].low;
        dst.high = src[0].high;
        ignoreScc = true;
        break;
    }
    case GcnOpcode::S_MOVK_I32: {
        dst.low.id = m_module.consti32(ins.control.sopk.simm);
        ignoreScc = true;
        break;
    }
    // ScalarBitLogic
    case GcnOpcode::S_AND_B32:
        dst.low.id = m_module.opBitwiseAnd(typeId, src[0].low.id, src[1].low.id);
        break;
    case GcnOpcode::S_OR_B32:
        dst.low.id = m_module.opBitwiseOr(typeId, src[0].low.id, src[1].low.id);
        break;
    case GcnOpcode::S_AND_B64: {
        dst.low.id = m_module.opBitwiseAnd(typeId, src[0].low.id, src[1].low.id);
        dst.high.id = m_module.opBitwiseAnd(typeId, src[0].high.id, src[1].high.id);
        break;
    }
    case GcnOpcode::S_OR_B64: {
        dst.low.id = m_module.opBitwiseOr(typeId, src[0].low.id, src[1].low.id);
        dst.high.id = m_module.opBitwiseOr(typeId, src[0].high.id, src[1].high.id);
        break;
    }
    case GcnOpcode::S_NOT_B64: {
        dst.low.id = m_module.opNot(typeId, src[0].low.id);
        dst.high.id = m_module.opNot(typeId, src[0].high.id);
        break;
    }
    case GcnOpcode::S_NOR_B64: {
        dst.low.id =
            m_module.opNot(typeId, m_module.opBitwiseOr(typeId, src[0].low.id, src[1].low.id));
        dst.high.id =
            m_module.opNot(typeId, m_module.opBitwiseOr(typeId, src[0].high.id, src[1].high.id));
        break;
    }
    case GcnOpcode::S_ANDN2_B64: {
        dst.low.id =
            m_module.opBitwiseAnd(typeId, src[0].low.id, m_module.opNot(typeId, src[1].low.id));
        dst.high.id =
            m_module.opBitwiseAnd(typeId, src[0].high.id, m_module.opNot(typeId, src[1].high.id));
        break;
    }
    case GcnOpcode::S_ORN2_B64: {
        dst.low.id =
            m_module.opBitwiseOr(typeId, src[0].low.id, m_module.opNot(typeId, src[1].low.id));
        dst.high.id =
            m_module.opBitwiseOr(typeId, src[0].high.id, m_module.opNot(typeId, src[1].high.id));
        break;
    }
    // ScalarBitManip
    case GcnOpcode::S_LSHL_B32: {
        u32 shift = m_module.opBitFieldUExtract(typeId, src[1].low.id, m_module.constu32(0),
                                                m_module.constu32(5));
        dst.low.id = m_module.opShiftLeftLogical(typeId, src[0].low.id, shift);
        break;
    }
    case GcnOpcode::S_LSHR_B32: {
        u32 shift = m_module.opBitFieldUExtract(typeId, src[1].low.id, m_module.constu32(0),
                                                m_module.constu32(5));
        dst.low.id = m_module.opShiftRightLogical(typeId, src[0].low.id, shift);
        break;
    }
    // ScalarBitField
    case GcnOpcode::S_BFE_U32: {
        u32 offset = m_module.opBitFieldUExtract(typeId, src[1].low.id, m_module.constu32(0),
                                                 m_module.constu32(5));
        u32 width = m_module.opBitFieldUExtract(typeId, src[1].low.id, m_module.constu32(16),
                                                m_module.constu32(7));
        u32 field = m_module.opShiftRightLogical(typeId, src[0].low.id, offset);
        u32 mask = m_module.opISub(typeId,
                                   m_module.opShiftLeftLogical(typeId, m_module.constu32(1), width),
                                   m_module.constu32(1));
        dst.low.id = m_module.opBitwiseAnd(typeId, field, mask);
        break;
    }
    default:
        UNIMPLEMENTED();
        break;
    }

    if (!ignoreScc) {
        emitUpdateScc(dst, ins.dst[0].type);
    }

    emitRegisterStore(ins.dst[0], dst);
}

void GcnCompiler::emitScalarMovRel(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarArith(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarAbs(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarMov(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarCmp(const GcnShaderInstruction& ins) {
    std::array<GcnRegisterValuePair, 2> src;

    if (ins.encoding == GcnInstEncoding::SOPC || ins.encoding == GcnInstEncoding::SOP2) {
        src[0] = emitRegisterLoad(ins.src[0]);
        src[1] = emitRegisterLoad(ins.src[1]);
    } else {
        // Deal with SOPK
        src[0] = emitRegisterLoad(ins.dst[0]);
        src[1].low = emitBuildConstValue(ins.control.sopk.simm, ins.dst[0].type);
    }

    const u32 typeId = m_module.defBoolType();

    GcnRegisterValuePair dst = {};
    dst.low.type.ctype = getDestinationType(ins.dst[0].type);
    dst.low.type.ccount = 1;
    dst.high.type = dst.low.type;

    u32 condition = 0;

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::S_CMP_EQ_I32:
        condition = m_module.opIEqual(typeId, src[0].low.id, src[1].low.id);
        break;
    case GcnOpcode::S_CMP_LG_U32:
    case GcnOpcode::S_CMP_LG_I32:
        condition = m_module.opINotEqual(typeId, src[0].low.id, src[1].low.id);
        break;
    default:
        UNIMPLEMENTED();
        break;
    }

    // Update SCC
    m_module.opStore(m_state.scc.id, condition);

    // SOP2 encoding requires us to save dst
    if (ins.encoding == GcnInstEncoding::SOP2) {
        emitRegisterStore(ins.dst[0], dst);
    }
}

void GcnCompiler::emitScalarSelect(const GcnShaderInstruction& ins) {
    const std::array<GcnRegisterValuePair, 2> src = {
        emitRegisterLoad(ins.src[0]),
        emitRegisterLoad(ins.src[1]),
    };

    GcnRegisterValuePair dst = {};
    dst.low.type.ctype = getDestinationType(ins.dst[0].type);
    dst.low.type.ccount = 1;
    dst.high.type = dst.low.type;

    const u32 typeId = getVectorTypeId(dst.low.type);

    u32 condition = m_module.opLoad(m_module.defBoolType(), m_state.scc.id);

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::S_CSELECT_B32:
        dst.low.id = m_module.opSelect(typeId, condition, src[0].low.id, src[1].low.id);
        break;
    case GcnOpcode::S_CSELECT_B64: {
        dst.low.id = m_module.opSelect(typeId, condition, src[0].low.id, src[1].low.id);
        dst.high.id = m_module.opSelect(typeId, condition, src[0].high.id, src[1].high.id);
        break;
    }
    default:
        UNIMPLEMENTED();
        break;
    }

    emitRegisterStore(ins.dst[0], dst);
}

void GcnCompiler::emitScalarBitLogic(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarBitManip(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarBitField(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarConv(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitScalarExecMask(const GcnShaderInstruction& ins) {
    auto src = emitRegisterLoad(ins.src[0]);

    // Save exec first
    auto exec = m_state.exec.emitLoad(GcnRegMask::firstN(2));
    emitRegisterStore(ins.dst[0], exec);

    GcnRegisterValuePair result = {};
    result.low.type = exec.low.type;
    result.high.type = exec.high.type;

    const u32 typeId = getVectorTypeId(result.low.type);

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::S_AND_SAVEEXEC_B64: {
        result.low.id = m_module.opBitwiseAnd(typeId, src.low.id, exec.low.id);
        result.high.id = m_module.opBitwiseAnd(typeId, src.high.id, exec.high.id);
        break;
    }
    default:
        UNIMPLEMENTED();
        break;
    }

    m_state.exec.emitStore(result, GcnRegMask::firstN(2));
}

void GcnCompiler::emitScalarQuadMask(const GcnShaderInstruction& ins) {
    auto src = emitRegisterLoad(ins.src[0]);

    GcnRegisterValuePair dst = {};
    dst.low.type.ctype = getDestinationType(ins.dst[0].type);
    dst.low.type.ccount = 1;
    dst.high.type = dst.low.type;

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::S_WQM_B64:
        dst.high = emitWholeQuadMode(src.high);
        [[fallthrough]];
    case GcnOpcode::S_WQM_B32:
        dst.low = emitWholeQuadMode(src.low);
        break;
    default:
        UNIMPLEMENTED();
        break;
    }

    emitRegisterStore(ins.dst[0], dst);
}

GcnRegisterValue GcnCompiler::emitWholeQuadMode(GcnRegisterValue src) {
    // Whole Quad Mode, 32 Bit
    // set mask for all 4 threads in each quad which has any threads active
    // for (q=0; q<8; q++)
    //     sdst[q*4+3:q*4].u = (ssrc[4*q+3:4*q].u != 0) ? 0xF : 0
    //
    // It seems we don't have an identical spirv instruction for gcn wqm.
    //
    // GLSL:
    // uint mask = xxx;
    // uint value = 0;
    // for(uint i = 0; i != 8; ++i)
    // {
    //     value |= ( (((mask >> i * 4) & 0xF) != 0 ? 0xFu : 0u) << (i * 4) );
    // }
    // mask = value;

    const u32 typeId = getScalarTypeId(GcnScalarType::Uint32);

    u32 mask = src.id;
    // Declare a temp value and initialize to zero.
    GcnRegisterInfo info;
    info.type.ctype = GcnScalarType::Uint32;
    info.type.ccount = 1;
    info.type.alength = 0;
    info.sclass = spv::StorageClassPrivate;
    // Declare a temp value and initialize to zero.
    u32 value = emitNewVariable(info);
    m_module.opStore(value, m_module.constu32(0));
    // Declare i and initialize to zero.
    u32 i = emitNewVariable(info);
    m_module.opStore(i, m_module.constu32(0));

    u32 labelBegin = m_module.allocateId();
    u32 labelContinue = m_module.allocateId();
    u32 labelEnd = m_module.allocateId();

    m_module.opBranch(labelBegin);
    m_module.opLabel(labelBegin);

    m_module.opLoopMerge(labelEnd, labelContinue, spv::LoopControlMaskNone);
    u32 labelLoop = m_module.allocateId();
    m_module.opBranch(labelLoop);
    m_module.opLabel(labelLoop);

    u32 iValue = m_module.opLoad(typeId, i);
    u32 condition = m_module.opINotEqual(m_module.defBoolType(), iValue, m_module.constu32(8));

    u32 labelDoWork = m_module.allocateId();
    m_module.opBranchConditional(condition, labelDoWork, labelEnd);
    m_module.opLabel(labelDoWork);

    iValue = m_module.opLoad(typeId, i);
    u32 shift = m_module.opIMul(typeId, iValue, m_module.constu32(4));
    u32 threadMask = m_module.opShiftRightLogical(typeId, mask, shift);
    threadMask = m_module.opBitwiseAnd(typeId, threadMask, m_module.constu32(0xF));

    condition = m_module.opINotEqual(m_module.defBoolType(), threadMask, m_module.constu32(0));
    u32 quad = m_module.opSelect(typeId, condition, m_module.constu32(0xF), m_module.constu32(0));

    iValue = m_module.opLoad(typeId, i);
    shift = m_module.opIMul(typeId, iValue, m_module.constu32(4));
    u32 shiftQuad = m_module.opShiftLeftLogical(typeId, quad, shift);
    u32 oldValue = m_module.opLoad(typeId, value);
    u32 newValue = m_module.opBitwiseOr(typeId, oldValue, shiftQuad);
    m_module.opStore(value, newValue);

    m_module.opBranch(labelContinue);
    m_module.opLabel(labelContinue);
    iValue = m_module.opLoad(typeId, i);
    iValue = m_module.opIAdd(typeId, iValue, m_module.constu32(1));
    m_module.opStore(i, iValue);
    m_module.opBranch(labelBegin);

    m_module.opLabel(labelEnd);

    GcnRegisterValue result;
    result.type = src.type;
    result.id = m_module.opLoad(typeId, value);
    return result;
}
} // namespace Shader::Gcn
