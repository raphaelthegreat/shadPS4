#pragma once

#include <vector>
#include "common/bit_field.h"
#include "common/types.h"

namespace Shader::Gcn {

enum class ShaderBinaryType : u8 {
    kPixelShader = 0,  ///< PS stage shader.
    kVertexShader = 1, ///< VS stage shader
    kExportShader = 2,
    kLocalShader = 3,
    kComputeShader = 4, ///< CS stage shader.
    kGeometryShader = 5,
    kUnknown = 6,
    kHullShader = 7,   ///< HS stage shader.
    kDomainShader = 8, ///< DS stage shader with embedded CS stage frontend shader.
};

enum ShaderInputUsageType : u8 {
    ImmResource = 0x00,                      ///< Immediate read-only buffer/texture descriptor.
    ImmSampler = 0x01,                       ///< Immediate sampler descriptor.
    ImmConstBuffer = 0x02,                   ///< Immediate constant buffer descriptor.
    ImmVertexBuffer = 0x03,                  ///< Immediate vertex buffer descriptor.
    ImmRwResource = 0x04,                    ///< Immediate read/write buffer/texture descriptor.
    ImmAluFloatConst = 0x05,                 ///< Immediate float const (scalar or vector).
    ImmAluBool32Const = 0x06,                ///< 32 immediate Booleans packed into one UINT.
    ImmGdsCounterRange = 0x07,
    ImmGdsMemoryRange = 0x08,                ///< Immediate UINT with GDS address range for storage.
    ImmGwsBase = 0x09,                       ///< Immediate UINT with GWS resource base offset.
    ImmShaderResourceTable = 0x0A,           ///< Pointer to read/write resource indirection table.
    ImmLdsEsGsSize = 0x0D,                   ///< Immediate LDS ESGS size used in on-chip GS
    SubPtrFetchShader = 0x12,                ///< Immediate fetch shader subroutine pointer.
    PtrResourceTable = 0x13,                 ///< Flat resource table pointer.
    PtrInternalResourceTable = 0x14,         ///< Flat internal resource table pointer.
    PtrSamplerTable = 0x15,                  ///< Flat sampler table pointer.
    PtrConstBufferTable = 0x16,              ///< Flat const buffer table pointer.
    PtrVertexBufferTable = 0x17,             ///< Flat vertex buffer table pointer.
    PtrSoBufferTable = 0x18,                 ///< Flat stream-out buffer table pointer.
    PtrRwResourceTable = 0x19,               ///< Flat read/write resource table pointer.
    PtrInternalGlobalTable = 0x1A,           ///< Internal driver table pointer.
    PtrExtendedUserData = 0x1B,              ///< Extended user data pointer.
    PtrIndirectResourceTable = 0x1C,         ///< Pointer to resource indirection table.
    PtrIndirectInternalResourceTable = 0x1D, ///< Pointer to internal resource indirection table.
    PtrIndirectRwResourceTable = 0x1E,       ///< Pointer to read/write resource indirection table.
};

struct ShaderBinaryInfo {
    u8 m_signature[7]; // 'OrbShdr'
    u8 m_version;      // ShaderBinaryInfoVersion

    unsigned int m_pssl_or_cg : 1; // 1 = PSSL / Cg, 0 = IL / shtb
    unsigned int m_cached : 1;     // 1 = when compile, debugging source was cached.  May only make
                                   // sense for PSSL=1
    u32 m_type : 4;                // See enum ShaderBinaryType
    u32 m_source_type : 2;         // See enum ShaderSourceType
    unsigned int m_length : 24; // Binary code length (does not include this structure or any of its
                                // preceding associated tables)

    u8 m_chunkUsageBaseOffsetInDW; // in DW, which starts at ((u32*)&ShaderBinaryInfo) -
                                   // m_chunkUsageBaseOffsetInDW; max is currently 7 dwords
                                   // (128 T# + 32 V# + 20 CB V# + 16 UAV T#/V#)
    u8 m_numInputUsageSlots;       // Up to 16 user data reg slots + 128 extended user data dwords
                                   // supported by CUE; up to 16 user data reg slots + 240 extended
                                   // user data dwords supported by Gnm::InputUsageSlot
    u8 m_isSrt : 1; // 1 if this shader uses shader resource tables and has an SrtDef table
                    // embedded below the input usage table and any extended usage info
    u8 m_isSrtUsedInfoValid : 1;  // 1 if SrtDef::m_isUsed=0 indicates an element is definitely
                                  // unused; 0 if SrtDef::m_isUsed=0 indicates only that the
                                  // element is not known to be used (m_isUsed=1 always
                                  // indicates a resource is known to be used)
    u8 m_isExtendedUsageInfo : 1; // 1 if this shader has extended usage info for the
                                  // InputUsage table embedded below the input usage table
    u8 m_reserved2 : 5;           // For future use
    u8 m_reserved3;               // For future use

    u32 m_shaderHash0; // Association hash first 4 bytes
    u32 m_shaderHash1; // Association hash second 4 bytes
    u32 m_crc32;       // crc32 of shader + this struct, just up till this field
};

struct alignas(4) InputUsageSlot {
    u8 m_usageType;     ///< From Gnm::ShaderInputUsageType.
    u8 m_apiSlot;       ///< API slot or chunk ID.
    u8 m_startRegister; ///< User data slot.

    union {
        struct {
            u8 m_registerCount : 1; ///< If 0, count is 4DW; if 1, count is 8DW. Other sizes
                                    ///< are defined by the usage type.
            u8 m_resourceType : 1;  ///< If 0, resource type <c>V#</c>; if 1, resource type
                                    ///< <c>T#</c>, in case of a Gnm::kShaderInputUsageImmResource.
            u8 m_reserved : 2;      ///< Unused; must be set to zero.
            u8 m_chunkMask : 4;     ///< Internal usage data.
        };
        u8 m_srtSizeInDWordMinusOne; ///< Size of the SRT data; used for
                                     ///< Gnm::kShaderInputUsageImmShaderResourceTable.
    };
};

using InputUsageSlotTable = std::vector<InputUsageSlot>;

struct VertexInputSemantic {
    u8 semantic;
    u8 dest_vgpr;
    u8 num_elements;
    u8 vsharp_index;
};

using VertexInputSemanticTable = std::vector<VertexInputSemantic>;

union VertexExportSemantic {
    BitField<0, 8, u16> semantic;
    BitField<8, 5, u16> out_index;
    BitField<14, 2, u16> export_f16;
};

struct PixelInputSemantic {
    union {
        struct {
            u16 m_semantic : 8;     ///< The semantic, matched against the semantic value in the
                                    ///< VertexExportSemantic table in the VS shader.
            u16 m_defaultValue : 2; ///< The default value supplied to the shader, if m_semantic is
                                    ///< not matched in the VS shader. 0={0,0,0,0}, 1={0,0,0,1.0},
                                    ///< 2={1.0,1.0,1.0,0}, 3={1.0,1.0,1.0,1.0}
            u16 m_isFlatShaded : 1; ///< if (m_interpF16 == 0) A bitflag that specifies whether
                                    ///< the value interpolation is constant in the shader. It
                                    ///< is ignored if <c><i>m_isCustom</i></c> is set;
                                    ///< otherwise, it  indicates that a shader reads only { P0
                                    ///< } and that some handling of infinite values in the
                                    ///< calculation of P1-P0 and P2-P0 can be disabled.
            u16 m_isLinear : 1;     ///< A bitflag that specifies whether the value interpolation
                                    ///< is linear in the shader. It is unused by the Gnm runtime.
            u16 m_isCustom : 1;     ///< if (m_interpF16 == 0) A bitflag that specifies whether the
                                    ///< value interpolation is custom in the shader. It determines
            ///< whether hardware subtraction should be disabled, supplying {
            ///< P0, P1, P2 } to the shader instead of { P0, P1-P0, P2-P0 }.
            u16 m_reserved : 3; ///< Unused; set to zero.
        };
        // NEO mode only:
        struct {
            u16 : 12;                 ///< Description to be specified.
            u16 m_defaultValueHi : 2; ///< if (m_interpF16 != 0) indicates the default value
                                      ///< supplied to the shader for the upper 16-bits if
                                      ///< m_semantic is not matched in the VS shader, and
                                      ///< m_defaultValue indicates the default value for the
                                      ///< lower 16-bits.
            u16 m_interpF16 : 2; ///< if (m_interpF16 == 0) this is a 32-bit float or custom value;
                                 ///< if (m_interpF16 & 1) the low 16-bits of this parameter expect
                                 ///< 16-bit float interpolation and/or default value; if
                                 ///< (m_interpF16 & 2) the high 16-bits of this parameter expect
                                 ///< 16-bit float interpolation and/or default value
        };
    };
};

/// Establish a semantic mapping between VertexExportSemantic and PixelInputSemantic.
/// Used to calculate pixel shader input location.
struct PixelSemanticMapping {
    u32 m_outIndex : 5;
    u32 m_isCustom : 1;
    u32 m_reserved0 : 2;
    u32 m_defaultValue : 2;
    u32 m_customOrFlat : 1; // Equal to ( m_isCustom | m_isFlatShaded ) from PixelInputSemantic
    u32 m_isLinear : 1;
    u32 m_isCustomDup : 1; // Same as m_isCustom
    u32 m_reserved1 : 19;
};

} // namespace Shader::Gcn
