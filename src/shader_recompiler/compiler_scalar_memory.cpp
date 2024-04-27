#include "common/assert.h"
#include "shader_recompiler/compiler.h"
#include "shader_recompiler/decoder.h"
#include "shader_recompiler/header.h"

namespace Shader::Gcn {

void GcnCompiler::emitScalarMemory(const GcnShaderInstruction& ins) {
    auto opClass = ins.opClass;
    switch (opClass) {
    case GcnInstClass::ScalarMemRd:
        this->emitScalarMemRd(ins);
        break;
    case GcnInstClass::ScalarMemUt:
        this->emitScalarMemUt(ins);
        break;
    default:
        UNREACHABLE();
    }
}

void GcnCompiler::emitScalarMemRd(const GcnShaderInstruction& ins) {
    auto smrd = gcnInstructionAs<GcnShaderInstSMRD>(ins);

    auto op = ins.opcode;
    switch (op) {
    case GcnOpcode::S_BUFFER_LOAD_DWORD:
    case GcnOpcode::S_BUFFER_LOAD_DWORDX2:
    case GcnOpcode::S_BUFFER_LOAD_DWORDX4:
    case GcnOpcode::S_BUFFER_LOAD_DWORDX8:
    case GcnOpcode::S_BUFFER_LOAD_DWORDX16: {
        GcnRegIndex index = {};
        // This is the sgpr pair index, so we need to multiply by 2
        index.regIdx = smrd.sbase.code * 2;
        if (smrd.control.imm) {
            index.offset = smrd.control.offset << 2;
            index.relReg = nullptr;
        } else {
            index.offset = 0;
            index.relReg = &smrd.offset;
        }

        emitScalarBufferLoad(index, smrd.sdst, smrd.control.count);
    } break;
    case GcnOpcode::S_LOAD_DWORDX4:
    case GcnOpcode::S_LOAD_DWORDX8:
        mapEudResource(ins);
        break;
    default:
        UNIMPLEMENTED();
        break;
    }
}

void GcnCompiler::mapEudResource(const GcnShaderInstruction& ins) {
    // Currently, the only usage case I found for S_LOAD_DWORDX4 S_LOAD_DWORDX8
    // is to load 4 or 8 sgpr sharp descriptors from EUD.

    ASSERT_MSG(ins.control.smrd.imm == 1, "TODO: support soffset eud load.");

    constexpr u8 kShaderInputUsagePtrExtendedUserData = 0x1B;

    const auto& resouceTable = m_header->getShaderResourceTable();
    auto iter =
        std::find_if(resouceTable.cbegin(), resouceTable.cend(), [](const ShaderResource& res) {
            return res.usage == kShaderInputUsagePtrExtendedUserData;
        });

    if (iter != resouceTable.end()) {
        // We found an EUD slot.
        // Check if it is used as the sbase of the instruction.
        u32 sbase = ins.src[0].code << 1;
        ASSERT_MSG(sbase == iter->startRegister, "S_LOAD_XXX is not used to load EUD.");

        u32 eudOffset = ins.control.smrd.offset;
        u32 eudReg = eudOffset + kMaxUserDataCount;
        u32 dstReg = ins.dst[0].code;

        auto resIt = std::find_if(
            resouceTable.cbegin(), resouceTable.cend(),
            [eudReg](const ShaderResource& res) { return res.startRegister == eudReg; });

        auto& res = *resIt;
        switch (res.type) {
        case vk::DescriptorType::eUniformBuffer:
        case vk::DescriptorType::eStorageBuffer:
            m_buffers.at(dstReg) = m_buffersDcl.at(eudReg);
            break;
        case vk::DescriptorType::eSampledImage:
        case vk::DescriptorType::eStorageImage:
            m_textures.at(dstReg) = m_texturesDcl.at(eudReg);
            break;
        case vk::DescriptorType::eSampler:
            m_samplers.at(dstReg) = m_samplersDcl.at(eudReg);
            break;
        case vk::DescriptorType{VK_DESCRIPTOR_TYPE_MAX_ENUM}:
            // skip
            break;
        default:
            UNREACHABLE_MSG("Not supported resouce type mapped.");
            break;
        }
    }
}

void GcnCompiler::emitScalarMemUt(const GcnShaderInstruction& ins) {
    UNIMPLEMENTED();
}
} // namespace Shader::Gcn
