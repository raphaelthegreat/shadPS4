#include "common/assert.h"
#include "shader_recompiler/compiler.h"
#include "shader_recompiler/decoder.h"

namespace Shader::Gcn {

void GcnCompiler::emitDataShare(const GcnShaderInstruction& ins) {
    auto opClass = ins.opClass;
    switch (opClass) {
    case GcnInstClass::DsIdxRd:
        this->emitDsIdxRd(ins);
        break;
    case GcnInstClass::DsIdxWr:
        this->emitDsIdxWr(ins);
        break;
    case GcnInstClass::DsIdxWrXchg:
        this->emitDsIdxWrXchg(ins);
        break;
    case GcnInstClass::DsIdxCondXchg:
        this->emitDsIdxCondXchg(ins);
        break;
    case GcnInstClass::DsIdxWrap:
        this->emitDsIdxWrap(ins);
        break;
    case GcnInstClass::DsAtomicArith32:
    case GcnInstClass::DsAtomicMinMax32:
        this->emitDsAtomicCommon(ins);
        break;
    case GcnInstClass::DsAtomicArith64:
        this->emitDsAtomicArith64(ins);
        break;
    case GcnInstClass::DsAtomicMinMax64:
        this->emitDsAtomicMinMax64(ins);
        break;
    case GcnInstClass::DsAtomicCmpSt32:
        this->emitDsAtomicCmpSt32(ins);
        break;
    case GcnInstClass::DsAtomicCmpSt64:
        this->emitDsAtomicCmpSt64(ins);
        break;
    case GcnInstClass::DsAtomicLogic32:
        this->emitDsAtomicLogic32(ins);
        break;
    case GcnInstClass::DsAtomicLogic64:
        this->emitDsAtomicLogic64(ins);
        break;
    case GcnInstClass::DsAppendCon:
        this->emitDsAppendCon(ins);
        break;
    case GcnInstClass::DsDataShareUt:
        this->emitDsDataShareUt(ins);
        break;
    case GcnInstClass::DsDataShareMisc:
        this->emitDsDataShareMisc(ins);
        break;
    case GcnInstClass::GdsSync:
        this->emitGdsSync(ins);
        break;
    case GcnInstClass::GdsOrdCnt:
        this->emitGdsOrdCnt(ins);
        break;
    default:
        UNREACHABLE();
    }
}

void GcnCompiler::emitDsIdxRd(const GcnShaderInstruction& ins) {
    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::DS_READ_I8:
    case GcnOpcode::DS_READ_U8:
    case GcnOpcode::DS_READ_I16:
    case GcnOpcode::DS_READ_U16:
    case GcnOpcode::DS_READ_B64:
    case GcnOpcode::DS_READ_B32:
    case GcnOpcode::DS_READ2_B32:
        this->emitDsRead(ins);
        break;
    default:
        UNIMPLEMENTED();
        break;
    }
}

void GcnCompiler::emitDsIdxWr(const GcnShaderInstruction& ins) {
    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::DS_WRITE_B8:
    case GcnOpcode::DS_WRITE_B16:
    case GcnOpcode::DS_WRITE_B32:
    case GcnOpcode::DS_WRITE_B64:
        this->emitDsWrite(ins);
        break;
    default:
        UNIMPLEMENTED();
        break;
    }
}

void GcnCompiler::emitDsIdxWrXchg(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsIdxCondXchg(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsIdxWrap(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicCommon(const GcnShaderInstruction& ins) {
    auto src = emitRegisterLoad(ins.src[0]);
    auto ptr = emitDsAccess(ins);

    GcnRegisterValuePair dst = {};
    dst.low.type.ctype = getDestinationType(ins.dst[0].type);
    dst.low.type.ccount = 1;
    dst.high.type = dst.low.type;

    bool saveOriginal = false;

    const u32 scopeId = m_module.constu32(spv::ScopeWorkgroup);
    const u32 semanticsId = m_module.constu32(spv::MemorySemanticsWorkgroupMemoryMask |
                                              spv::MemorySemanticsAcquireReleaseMask);

    const u32 typeId = getScalarTypeId(GcnScalarType::Uint32);

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::DS_ADD_RTN_U32:
        saveOriginal = true;
        [[fallthrough]];
    case GcnOpcode::DS_ADD_U32:
        dst.low.id = m_module.opAtomicIAdd(typeId, ptr[0].id, scopeId, semanticsId, src.low.id);
        break;
    case GcnOpcode::DS_MIN_RTN_U32:
        saveOriginal = true;
        [[fallthrough]];
    case GcnOpcode::DS_MIN_U32:
        dst.low.id = m_module.opAtomicUMin(typeId, ptr[0].id, scopeId, semanticsId, src.low.id);
        break;
    case GcnOpcode::DS_MAX_RTN_U32:
        saveOriginal = true;
        [[fallthrough]];
    case GcnOpcode::DS_MAX_U32:
        dst.low.id = m_module.opAtomicUMax(typeId, ptr[0].id, scopeId, semanticsId, src.low.id);
        break;
    default:
        UNIMPLEMENTED();
        break;
    }

    if (saveOriginal) {
        emitRegisterStore(ins.dst[0], dst);
    }
}

void GcnCompiler::emitDsAtomicArith32(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicArith64(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicMinMax32(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicMinMax64(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicCmpSt32(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicCmpSt64(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicLogic32(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAtomicLogic64(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsAppendCon(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitDsDataShareUt(const GcnShaderInstruction& ins) {
    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::DS_SWIZZLE_B32:
        emitDsSwizzle(ins);
        break;
    default:
        UNIMPLEMENTED();
        break;
    }
}

void GcnCompiler::emitDsDataShareMisc(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitGdsSync(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

void GcnCompiler::emitGdsOrdCnt(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}

std::array<GcnRegisterPointer, 4> GcnCompiler::emitDsAccess(const GcnShaderInstruction& ins) {
    // one u8, s8, u16, s16  : result[0]
    // one u32 result[0]
    // two u32 result[0] result[1]
    // one u64 result[0] result[1]
    // two u64 (result[0] result[1]) (result[2] result[3])

    auto ds = gcnInstructionAs<GcnShaderInstDS>(ins);
    const auto& control = ds.control;

    ASSERT_MSG(control.relative == 0, "TODO: support relative offset mode.");

    ASSERT_MSG(control.gds == 0, "TODO: support GDS.");
    u32 dsBufferId = control.gds ? 0 : m_lds;

    std::array<u32, 2> offsetId = {};
    if (!control.dual) {
        u32 offset = control.offset1 << 8 | control.offset0;
        offsetId[0] = m_module.constu32(offset);
    } else {
        ASSERT_MSG(control.offset0 != control.offset1, "TODO: support equal dual offsets");
        offsetId[0] = m_module.constu32(control.offset0);
        offsetId[1] = m_module.constu32(control.offset1);
    }

    const u32 uintTypeId = getScalarTypeId(GcnScalarType::Uint32);
    u32 ptrTypeId = m_module.defPointerType(uintTypeId, spv::StorageClassWorkgroup);

    auto vbindex = emitRegisterLoad(ds.addr);

    std::array<u32, 2> regionAddr = {};
    if (!control.dual) {
        // TODO:
        // Do we really need to align?
        regionAddr[0] = m_module.opIAdd(uintTypeId, offsetId[0], vbindex.low.id);
    } else {
        u32 opDataSize = m_module.constu32(control.size);
        if (control.stride) {
            opDataSize = m_module.opIMul(uintTypeId, opDataSize, m_module.constu32(64));
        }

        regionAddr[0] = m_module.opIAdd(
            uintTypeId, m_module.opIMul(uintTypeId, offsetId[0], opDataSize), vbindex.low.id);
        regionAddr[1] = m_module.opIAdd(
            uintTypeId, m_module.opIMul(uintTypeId, offsetId[1], opDataSize), vbindex.low.id);
    }

    std::array<GcnRegisterPointer, 4> result = {};

    GcnRegisterPointer ptr;
    ptr.type.ctype = GcnScalarType::Uint32;
    ptr.type.ccount = 1;

    // LDS and GDS are all uint arrays.
    u32 resultIdx = 0;
    for (u32 i = 0; i != 2; ++i) {
        u32 uintIdx = m_module.opUDiv(uintTypeId, regionAddr[i], m_module.constu32(4));
        ptr.id = m_module.opAccessChain(ptrTypeId, dsBufferId, 1, &uintIdx);
        result[resultIdx++] = ptr;
        if (control.size == 8) {
            u32 nextIdx = m_module.opIAdd(uintTypeId, uintIdx, m_module.constu32(1));
            ptr.id = m_module.opAccessChain(ptrTypeId, dsBufferId, 1, &nextIdx);
            result[resultIdx++] = ptr;
        }

        if (!control.dual) {
            break;
        }
    }

    return result;
}

void GcnCompiler::emitDsRead(const GcnShaderInstruction& ins) {
    auto ptrList = emitDsAccess(ins);

    GcnRegisterValuePair dst = {};
    dst.low.type.ctype = getDestinationType(ins.dst[0].type);
    dst.low.type.ccount = 1;
    dst.high.type = dst.low.type;

    // We need to fix dst type for some instructions so make a copy.
    auto dstReg = ins.dst[0];

    u32 typeId = getScalarTypeId(dst.low.type.ctype);

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::DS_READ_I8:
    case GcnOpcode::DS_READ_U8:
    case GcnOpcode::DS_READ_I16:
    case GcnOpcode::DS_READ_U16:
        UNIMPLEMENTED();
        break;
    case GcnOpcode::DS_READ_B64:
        dst.high.id = m_module.opLoad(typeId, ptrList[1].id);
        [[fallthrough]];
    case GcnOpcode::DS_READ_B32:
        dst.low.id = m_module.opLoad(typeId, ptrList[0].id);
        break;
    case GcnOpcode::DS_READ2_B32: {
        // Fix dst type so we don't miss high part.
        dstReg.type = GcnScalarType::Uint64;
        dst.low.id = m_module.opLoad(typeId, ptrList[0].id);
        dst.high.id = m_module.opLoad(typeId, ptrList[1].id);
    } break;
    default:
        UNIMPLEMENTED();
        break;
    }

    emitRegisterStore(dstReg, dst);
}

void GcnCompiler::emitDsWrite(const GcnShaderInstruction& ins) {
    auto ptrList = emitDsAccess(ins);

    std::array<GcnRegisterValuePair, 2> src;
    for (u32 i = 0; i != 2; ++i) {
        // DS encoding src starts from src[1]
        src[i] = emitRegisterLoad(ins.src[1 + i]);
        if (!ins.control.ds.dual) {
            break;
        }
    }

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::DS_WRITE_B8:
    case GcnOpcode::DS_WRITE_B16:
        UNIMPLEMENTED();
        break;
    case GcnOpcode::DS_WRITE_B64:
        emitValueStore(ptrList[1], src[0].high, GcnRegMask::select(0));
        [[fallthrough]];
    case GcnOpcode::DS_WRITE_B32:
        emitValueStore(ptrList[0], src[0].low, GcnRegMask::select(0));
        break;
    default:
        UNIMPLEMENTED();
        break;
    }
}

void GcnCompiler::emitDsSwizzle(const GcnShaderInstruction& ins) {
    //  // QDMode - full data sharing in 4 consecutive threads
    //  if (offset[15]) {
    //       for (i = 0; i < 32; i+=4) {
    //            thread_out[i+0] = thread_valid[i+offset[1:0]] ? thread_in[i+offset[1:0]] : 0;
    //            thread_out[i+1] = thread_valid[i+offset[3:2]] ? thread_in[i+offset[3:2]] : 0;
    //            thread_out[i+2] = thread_valid[i+offset[5:4]] ? thread_in[i+offset[5:4]] : 0;
    //            thread_out[i+3] = thread_valid[i+offset[7:6]] ? thread_in[i+offset[7:6]] : 0;
    //       }
    //  }
    //
    //  // BitMode - limited data sharing in 32 consecutive threads
    //  else {
    //       and_mask = offset[4:0];
    //       or_mask = offset[9:5];
    //       xor_mask = offset[14:10];
    //       for (i = 0; i < 32; i++)
    //       {
    //           j = ((i & (0x20 | and_mask)) | or_mask) ^ xor_mask;
    //           thread_out[i] = thread_valid[j] ? thread_in[j] : 0;
    //       }
    //  }
    //  // Same shuffle applied to the second half of wavefront

    enum DsMode : u8 {
        BitMode = 0,
        QdMode = 1,
    };

    union DsPattern {
        struct Mode {
            u16 : 15;
            u16 mode : 1;
        } m;

        struct QdMode {
            u16 lane0 : 2;
            u16 lane1 : 2;
            u16 lane2 : 2;
            u16 lane3 : 2;
            u16 : 7;
            u16 mode : 1;
        } qd;

        struct BitMode {
            u16 mask_and : 5;
            u16 mask_or : 5;
            u16 mask_xor : 5;
            u16 mode : 1;
        } bit;
    };

    m_module.enableCapability(spv::CapabilityGroupNonUniformShuffle);

    auto src = emitRegisterLoad(ins.src[0]);

    GcnRegisterValuePair dst = {};
    dst.low.type.ctype = getDestinationType(ins.dst[0].type);
    dst.low.type.ccount = 1;
    dst.high.type = dst.low.type;

    const u32 typeId = getScalarTypeId(GcnScalarType::Uint32);

    auto invocationId =
        emitCommonSystemValueLoad(GcnSystemValue::SubgroupInvocationID, GcnRegMask::select(0));

    u16 offset = u16(ins.control.ds.offset1) << 8 | u16(ins.control.ds.offset0);
    DsPattern* pat = reinterpret_cast<DsPattern*>(&offset);
    u32 laneIndex = 0;

    if (pat->m.mode == QdMode) {
        u32 mod = m_module.opUMod(typeId, invocationId.id, m_module.constu32(4));

        std::array<u32, 4> offs = {
            m_module.constu32(pat->qd.lane0), m_module.constu32(pat->qd.lane1),
            m_module.constu32(pat->qd.lane2), m_module.constu32(pat->qd.lane3)};

        u32 arrayTypeId = getArrayTypeId({GcnScalarType::Uint32, 1, offs.size()});

        u32 laneOffsetArray = m_module.constComposite(arrayTypeId, offs.size(), offs.data());

        u32 varId =
            m_module.newVarInit(m_module.defPointerType(arrayTypeId, spv::StorageClassPrivate),
                                spv::StorageClassPrivate, laneOffsetArray);

        u32 ptr = m_module.opAccessChain(m_module.defPointerType(typeId, spv::StorageClassPrivate),
                                         varId, 1, &mod);
        u32 laneOffset = m_module.opLoad(typeId, ptr);
        u32 laneBase = m_module.opUDiv(typeId, invocationId.id, m_module.constu32(4));
        laneIndex = m_module.opIAdd(typeId, laneBase, laneOffset);
    } else {
        u32 maskAnd = m_module.constu32(pat->bit.mask_and);
        u32 maskOr = m_module.constu32(pat->bit.mask_or);
        u32 maskXor = m_module.constu32(pat->bit.mask_xor);

        // or 0x20 to protect high 32 lanes if subgroup size is greater than 32
        maskAnd = m_module.opBitwiseOr(typeId, m_module.constu32(0x20), maskAnd);

        u32 valAnd = m_module.opBitwiseAnd(typeId, invocationId.id, maskAnd);
        u32 valOr = m_module.opBitwiseOr(typeId, valAnd, maskOr);
        laneIndex = m_module.opBitwiseXor(typeId, valOr, maskXor);
    }

    u32 laneValue = m_module.opGroupNonUniformShuffle(typeId, m_module.constu32(spv::ScopeSubgroup),
                                                      src.low.id, laneIndex);

    // Detect if src lane is active,
    // if it's inactive, we need to return 0 for dst.
    u32 laneMask = m_module.opShiftLeftLogical(typeId, m_module.constu32(1), laneIndex);
    auto exec = m_state.exec.emitLoad(GcnRegMask::select(0));

    u32 laneActive = m_module.opINotEqual(m_module.defBoolType(),
                                          m_module.opBitwiseAnd(typeId, exec.low.id, laneMask),
                                          m_module.constu32(0));

    dst.low.id = m_module.opSelect(typeId, laneActive, laneValue, m_module.constu32(0));

    emitRegisterStore(ins.dst[0], dst);
}
} // namespace Shader::Gcn
