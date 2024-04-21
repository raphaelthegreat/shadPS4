#pragma once

#include <limits>
#include <vector>
#include "common/enum.h"
#include "shader_recompiler/gcn_enum.h"

namespace Shader::Gcn {

constexpr u32 GcnMaxSrcCount = 4;
constexpr u32 GcnMaxDstCount = 2;

struct GcnInstFormat {
    GcnInstClass instructionClass = GcnInstClass::Undefined;
    GcnInstCategory instructionCategory = GcnInstCategory::Undefined;

    u32 srcCount = 0;
    u32 dstCount = 0;

    GcnScalarType srcType = GcnScalarType::Undefined;
    GcnScalarType dstType = GcnScalarType::Undefined;
};

/**
 * \brief Input modifiers
 *
 * These are applied after loading
 * an operand register.
 */
enum class GcnInputModifier : u32 {
    Neg = 0,
    Abs = 1,
};

using GcnInputModifiers = Common::Flags<GcnInputModifier>;

/**
 * \brief Output modifiers
 *
 * These are applied before storing
 * an operand register.
 */
struct GcnOutputModifiers {
    bool clamp = false;
    float multiplier = std::numeric_limits<float>::quiet_NaN();
};

struct GcnInstOperand {
    GcnOperandField field = GcnOperandField::Undefined;
    GcnScalarType type = GcnScalarType::Undefined;

    GcnInputModifiers inputModifier = {};
    GcnOutputModifiers outputModifier = {};

    union {
        u32 code = 0xFFFFFFFF;
        u32 literalConst;
    };
};

struct GcnInstControlSOPK {
    u32 simm : 16;
    u32 : 16;
};

struct GcnInstControlSOPP {
    u32 simm : 16;
    u32 : 16;
};

struct GcnInstControlVOP3 {
    uint64_t : 8;
    uint64_t abs : 3;
    uint64_t clmp : 1;
    uint64_t : 47;
    uint64_t omod : 2;
    uint64_t neg : 3;
};

struct GcnInstControlSMRD {
    u32 offset : 8;
    u32 imm : 1;
    u32 count : 5;
    u32 : 18;
};

struct GcnInstControlMUBUF {
    uint64_t offset : 12;
    uint64_t offen : 1;
    uint64_t idxen : 1;
    uint64_t glc : 1;
    uint64_t : 1;
    uint64_t lds : 1;
    uint64_t : 37;
    uint64_t slc : 1;
    uint64_t tfe : 1;
    uint64_t count : 3;
    uint64_t size : 5;
};

struct GcnInstControlMTBUF {
    uint64_t offset : 12;
    uint64_t offen : 1;
    uint64_t idxen : 1;
    uint64_t glc : 1;
    uint64_t : 4;
    uint64_t dfmt : 4;
    uint64_t nfmt : 3;
    uint64_t : 28;
    uint64_t slc : 1;
    uint64_t tfe : 1;
    uint64_t count : 3;
    uint64_t size : 5;
};

struct GcnInstControlMIMG {
    uint64_t : 8;
    uint64_t dmask : 4;
    uint64_t unrm : 1;
    uint64_t glc : 1;
    uint64_t da : 1;
    uint64_t r128 : 1;
    uint64_t tfe : 1;
    uint64_t lwe : 1;
    uint64_t : 7;
    uint64_t slc : 1;
    uint64_t mod : 32;
    uint64_t : 6;
};

struct GcnInstControlDS {
    uint64_t offset0 : 8;
    uint64_t offset1 : 8;
    uint64_t : 1;
    uint64_t gds : 1;
    uint64_t dual : 1;
    uint64_t sign : 1;
    uint64_t relative : 1;
    uint64_t stride : 1;
    uint64_t size : 4;
    uint64_t : 38;
};

struct GcnInstControlVINTRP {
    u32 : 8;
    u32 chan : 2;
    u32 attr : 6;
    u32 : 16;
};

struct GcnInstControlEXP {
    uint64_t en : 4;
    uint64_t target : 6;
    uint64_t compr : 1;
    uint64_t done : 1;
    uint64_t vm : 1;
    uint64_t reserved : 51;
};

union GcnInstControl {
    GcnInstControlSOPK sopk;
    GcnInstControlSOPP sopp;
    GcnInstControlVOP3 vop3;
    GcnInstControlSMRD smrd;
    GcnInstControlMUBUF mubuf;
    GcnInstControlMTBUF mtbuf;
    GcnInstControlMIMG mimg;
    GcnInstControlDS ds;
    GcnInstControlVINTRP vintrp;
    GcnInstControlEXP exp;
};

struct GcnShaderInstruction {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstEncoding encoding;
    GcnInstClass opClass;
    GcnInstCategory category;
    GcnInstControl control;

    u32 srcCount;
    u32 dstCount;

    GcnInstOperand src[GcnMaxSrcCount];
    GcnInstOperand dst[GcnMaxDstCount];
};

using GcnInstructionList = std::vector<GcnShaderInstruction>;

struct GcnShaderInstSOP1 {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;

    GcnInstOperand ssrc0;
    GcnInstOperand sdst;
};

struct GcnShaderInstSOP2 {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;

    GcnInstOperand ssrc0;
    GcnInstOperand ssrc1;
    GcnInstOperand sdst;
};

struct GcnShaderInstSOPK {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlSOPK control;

    GcnInstOperand sdst;
};

struct GcnShaderInstSOPC {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;

    GcnInstOperand ssrc0;
    GcnInstOperand ssrc1;
};

struct GcnShaderInstSOPP {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlSOPP control;
};

struct GcnShaderInstVOP1 {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;

    GcnInstOperand src0;
    GcnInstOperand vdst;
};

struct GcnShaderInstVOP2 {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;

    GcnInstOperand src0;
    GcnInstOperand vsrc1;
    GcnInstOperand vdst;
};

struct GcnShaderInstVOP3 {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlVOP3 control;

    GcnInstOperand src0;
    GcnInstOperand src1;
    GcnInstOperand src2;
    GcnInstOperand vdst;
    GcnInstOperand sdst;
};

struct GcnShaderInstVOPC {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;

    GcnInstOperand src0;
    GcnInstOperand vsrc1;
};

struct GcnShaderInstSMRD {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlSMRD control;

    GcnInstOperand offset;
    GcnInstOperand sbase;
    GcnInstOperand sdst;
};

struct GcnShaderInstMUBUF {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlMUBUF control;

    GcnInstOperand vaddr;
    GcnInstOperand vdata;
    GcnInstOperand srsrc;
    GcnInstOperand soffset;
};

struct GcnShaderInstMTBUF {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlMTBUF control;

    GcnInstOperand vaddr;
    GcnInstOperand vdata;
    GcnInstOperand srsrc;
    GcnInstOperand soffset;
};

struct GcnShaderInstMIMG {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlMIMG control;

    GcnInstOperand vaddr;
    GcnInstOperand vdata;
    GcnInstOperand srsrc;
    GcnInstOperand ssamp;
};

struct GcnShaderInstVINTRP {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlVINTRP control;

    GcnInstOperand vsrc;
    GcnInstOperand vdst;
};

struct GcnShaderInstDS {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlDS control;

    GcnInstOperand addr;
    GcnInstOperand data0;
    GcnInstOperand data1;
    GcnInstOperand vdst;
};

struct GcnShaderInstEXP {
    GcnOpcode opcode;
    u32 length; // in bytes
    GcnInstClass opClass;
    GcnInstControlEXP control;
    GcnExportTarget target;

    GcnInstOperand vsrc0;
    GcnInstOperand vsrc1;
    GcnInstOperand vsrc2;
    GcnInstOperand vsrc3;
};

GcnInstFormat gcnInstructionFormat(GcnInstEncoding encoding, u32 opcode);

} // namespace Shader::Gcn
