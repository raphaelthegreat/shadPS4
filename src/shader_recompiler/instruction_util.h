#pragma once

#include "shader_recompiler/instruction.h"

namespace Shader::Gcn {

bool isVop3BEncoding(GcnOpcode opcode);

bool isUnconditionalBranch(const GcnShaderInstruction& ins);

bool isConditionalBranch(const GcnShaderInstruction& ins);

bool isBranchInstruction(const GcnShaderInstruction& ins);

bool isForkInstruction(const GcnShaderInstruction& ins);

/**
 * \brief Is basic block terminate instruction
 */
bool isTerminateInstruction(const GcnShaderInstruction& ins);

u32 calculateBranchTarget(u32 pc, const GcnShaderInstruction& ins);

/**
 * \brief Is image access with sampling
 */
bool isImageAccessSampling(const GcnShaderInstruction& ins);

/**
 * \brief Is image access without sampling
 *
 * Note it's not equal to !isImageAccessSampling(ins);
 */
bool isImageAccessNoSampling(const GcnShaderInstruction& ins);

/**
 * \brief Is UAV read instruction
 */
bool isUavReadAccess(const GcnShaderInstruction& ins);

} // namespace Shader::Gcn
