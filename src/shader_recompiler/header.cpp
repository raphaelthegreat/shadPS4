#include "common/assert.h"
#include "shader_recompiler/gcn_constants.h"
#include "shader_recompiler/decoder.h"
#include "shader_recompiler/header.h"
#include "shader_recompiler/instruction_util.h"
#include "shader_recompiler/program_info.h"

namespace Shader::Gcn {

typedef enum ShaderInputUsageType
{
    kShaderInputUsageImmResource                = 0x00, ///< Immediate read-only buffer/texture descriptor.
    kShaderInputUsageImmSampler			        = 0x01, ///< Immediate sampler descriptor.
    kShaderInputUsageImmConstBuffer             = 0x02, ///< Immediate constant buffer descriptor.
    kShaderInputUsageImmVertexBuffer            = 0x03, ///< Immediate vertex buffer descriptor.
    kShaderInputUsageImmRwResource				= 0x04, ///< Immediate read/write buffer/texture descriptor.
    kShaderInputUsageImmAluFloatConst		    = 0x05, ///< Immediate float const (scalar or vector).
    kShaderInputUsageImmAluBool32Const		    = 0x06, ///< 32 immediate Booleans packed into one UINT.
    kShaderInputUsageImmGdsCounterRange	        = 0x07, ///< Immediate UINT with GDS address range for counters (used for append/consume buffers).
    kShaderInputUsageImmGdsMemoryRange		    = 0x08, ///< Immediate UINT with GDS address range for storage.
    kShaderInputUsageImmGwsBase                 = 0x09, ///< Immediate UINT with GWS resource base offset.
    kShaderInputUsageImmShaderResourceTable     = 0x0A, ///< Pointer to read/write resource indirection table.
    kShaderInputUsageImmLdsEsGsSize             = 0x0D, ///< Immediate LDS ESGS size used in on-chip GS
                                            // Skipped several items here...
    kShaderInputUsageSubPtrFetchShader		    = 0x12, ///< Immediate fetch shader subroutine pointer.
    kShaderInputUsagePtrResourceTable           = 0x13, ///< Flat resource table pointer.
    kShaderInputUsagePtrInternalResourceTable   = 0x14, ///< Flat internal resource table pointer.
    kShaderInputUsagePtrSamplerTable		    = 0x15, ///< Flat sampler table pointer.
    kShaderInputUsagePtrConstBufferTable	    = 0x16, ///< Flat const buffer table pointer.
    kShaderInputUsagePtrVertexBufferTable       = 0x17, ///< Flat vertex buffer table pointer.
    kShaderInputUsagePtrSoBufferTable		    = 0x18, ///< Flat stream-out buffer table pointer.
    kShaderInputUsagePtrRwResourceTable		    = 0x19, ///< Flat read/write resource table pointer.
    kShaderInputUsagePtrInternalGlobalTable     = 0x1A, ///< Internal driver table pointer.
    kShaderInputUsagePtrExtendedUserData        = 0x1B, ///< Extended user data pointer.
    kShaderInputUsagePtrIndirectResourceTable   = 0x1C, ///< Pointer to resource indirection table.
    kShaderInputUsagePtrIndirectInternalResourceTable = 0x1D, ///< Pointer to internal resource indirection table.
    kShaderInputUsagePtrIndirectRwResourceTable = 0x1E, ///< Pointer to read/write resource indirection table.
} ShaderInputUsageType;

GcnBinaryInfo::GcnBinaryInfo(const void* shaderCode) {
    const u32* token = reinterpret_cast<const u32*>(shaderCode);
    constexpr u32 tokenMovVccHi = 0xBEEB03FF;

    // First instruction should be
    // s_mov_b32 vcc_hi, sizeInWords
    // currently I didn't meet other cases,
    // but if it is, we can still search for the header magic 'OrbShdr'
    ASSERT_MSG(token[0] == tokenMovVccHi, "First instruction is not s_mov_b32 vcc_hi, #imm");

    m_binInfo = reinterpret_cast<const ShaderBinaryInfo*>(token + (token[1] + 1) * 2);
}

GcnBinaryInfo::~GcnBinaryInfo() = default;

ShaderBinaryType GcnBinaryInfo::stage() const {
    return static_cast<ShaderBinaryType>(m_binInfo->m_type);
}

GcnHeader::GcnHeader(const u8* shaderCode) {
    parseHeader(shaderCode);
    extractResourceTable(shaderCode);
}

GcnHeader::~GcnHeader() = default;

GcnProgramType GcnHeader::type() const {
    GcnProgramType result = GcnProgramType::VertexShader;
    ShaderBinaryType binType = static_cast<ShaderBinaryType>(m_binInfo.m_type);
    switch (binType) {
    case ShaderBinaryType::kPixelShader:
        result = GcnProgramType::PixelShader;
        break;
    case ShaderBinaryType::kVertexShader:
        result = GcnProgramType::VertexShader;
        break;
    case ShaderBinaryType::kComputeShader:
        result = GcnProgramType::ComputeShader;
        break;
    case ShaderBinaryType::kGeometryShader:
        result = GcnProgramType::GeometryShader;
        break;
    case ShaderBinaryType::kHullShader:
        result = GcnProgramType::HullShader;
        break;
    case ShaderBinaryType::kDomainShader:
        result = GcnProgramType::DomainShader;
        break;
    case ShaderBinaryType::kLocalShader:
    case ShaderBinaryType::kExportShader:
    case ShaderBinaryType::kUnknown:
    default:
        UNREACHABLE_MSG("Unknown shader type.");
        break;
    }
    return result;
}

void GcnHeader::parseHeader(const u8* shaderCode) {
    GcnBinaryInfo info(shaderCode);
    const ShaderBinaryInfo* binaryInfo = info.info();
    std::memcpy(&m_binInfo, binaryInfo, sizeof(ShaderBinaryInfo));

    // Get usage masks and input usage slots
    u32 const* usageMasks = reinterpret_cast<u32 const*>(
        (u8 const*)binaryInfo - binaryInfo->m_chunkUsageBaseOffsetInDW * 4);
    int32_t inputUsageSlotsCount = binaryInfo->m_numInputUsageSlots;
    InputUsageSlot const* inputUsageSlots =
        (InputUsageSlot const*)usageMasks - inputUsageSlotsCount;

    m_inputUsageSlotTable.reserve(inputUsageSlotsCount);
    for (u32 i = 0; i != inputUsageSlotsCount; ++i) {
        m_inputUsageSlotTable.emplace_back(inputUsageSlots[i]);
    }
}

void GcnHeader::extractResourceTable(const u8* code) {
    // We can't distinguish some of the resource type without iterate
    // through all shader instructions.
    // For example, a T# in an ImmResource slot
    // may be either a sampled image or a storage image.
    // If it is accessed via an IMAGE_LOAD_XXX instruction,
    // then we can say it is a storage image.
    auto typeInfo = analyzeResourceType(code);

    m_resourceTable.reserve(m_inputUsageSlotTable.size());

    for (const auto& slot : m_inputUsageSlotTable) {
        GcnShaderResource res = {};

        res.usage = slot.m_usageType;
        res.inEud = (slot.m_startRegister >= kMaxUserDataCount);
        res.startRegister = slot.m_startRegister;

        if (res.inEud) {
            res.eudOffsetInDwords = slot.m_startRegister - kMaxUserDataCount;
        }

        bool isVSharp = (slot.m_resourceType == 0);

        switch (slot.m_usageType) {
        case kShaderInputUsageImmResource:
        case kShaderInputUsageImmRwResource: {
            if (isVSharp) {
                // We use ssbo instead of ubo no matter
                // it is read-only or read-write,
                // since the buffer could be pretty large.
                res.type = vk::DescriptorType::eStorageBuffer;
            } else {
                auto iter = typeInfo.find(res.startRegister);
                bool isUav = iter != typeInfo.end();
                if (!isUav) {
                    res.type = vk::DescriptorType::eSampledImage;
                } else {
                    // It's annoying to translate image_load_mip
                    // instruction.
                    // Normally, we should use OpImageRead/Write,
                    // to access UAVs, but these two opcodes doesn't
                    // support LOD operands.
                    // On AMD GPU, we have SPV_AMD_shader_image_load_store_lod to ease things,
                    // but no identical method for Nvidia GPU.
                    // So we need to use OpImageFetch to replace OpImageRead,
                    // thus to declare the image as sampled.
                    // (Well OpImageFetch will be translated back to image_load_mip
                    // eventually on AMD GPU, tested on RadeonGPUAnalyzer,
                    // not sure for Nvidia.)
                    // For image_store_mip, I don't have a good idea as of now.
                    bool isUavRead = iter->second;
                    if (isUavRead) {
                        res.type = vk::DescriptorType::eSampledImage;
                    } else {
                        res.type = vk::DescriptorType::eStorageImage;
                    }
                }
            }

            res.sizeInDwords = slot.m_registerCount == 0 ? 4 : 8;
            break;
        }
        case kShaderInputUsageImmConstBuffer: {
            res.type = vk::DescriptorType::eUniformBuffer;
            res.sizeInDwords = kDwordSizeConstantBuffer;
            break;
        }
        case kShaderInputUsageImmSampler: {
            res.type = vk::DescriptorType::eSampler;
            res.sizeInDwords = kDwordSizeSampler;
            break;
        }
        case kShaderInputUsagePtrExtendedUserData:
        case kShaderInputUsageSubPtrFetchShader:
        case kShaderInputUsagePtrVertexBufferTable: {
            // This is not really a resource
            res.type = vk::DescriptorType{VK_DESCRIPTOR_TYPE_MAX_ENUM};
            res.sizeInDwords = 2;
            break;
        }
        default:
            UNREACHABLE_MSG("Not supported usage type.");
            break;
        }

        m_resourceTable.push_back(res);
    }
}

GcnHeader::ResourceTypeInfo GcnHeader::analyzeResourceType(const u8* code) {
    const u32* start = reinterpret_cast<const u32*>(code);
    const u32* end = reinterpret_cast<const u32*>(code + m_binInfo.m_length);
    GcnCodeSlice slice(start, end);

    GcnDecodeContext decoder;
    GcnHeader::ResourceTypeInfo result;
    while (!slice.atEnd()) {
        decoder.decodeInstruction(slice);
        auto& ins = decoder.getInstruction();
        if (isImageAccessNoSampling(ins)) {
            u32 startRegister = ins.src[2].code << 2;
            result[startRegister] = isUavReadAccess(ins);
        }
    }

    return result;
}

} // namespace Shader::Gcn
