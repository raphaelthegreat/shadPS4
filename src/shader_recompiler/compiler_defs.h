#pragma once

#include <numbers>
#include <string>
#include "shader_recompiler/gcn_enum.h"
#include "shader_recompiler/instruction.h"
#include "shader_recompiler/shader_meta.h"
#include "shader_recompiler/spirv/spirv.hpp"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Shader::Gcn {

struct GcnInstOperand;

constexpr size_t GcnMaxInterfaceRegs = 32;
constexpr size_t GcnMaxSGPR = 104;
constexpr size_t GcnMaxVGPR = 256;
constexpr size_t GcnMaxOperandCount = 5;
constexpr size_t GcnMaxExportParam = 32;
constexpr size_t GcnMaxResourceReg = 64;

constexpr size_t GcnExpPos0 = 12;
constexpr size_t GcnExpParam0 = 32;

constexpr double GcnPi = std::numbers::pi;

enum class GcnZeroTest : u32 {
    TestZ = 0,
    TestNz = 1,
};

/**
 * \brief Vector type
 *
 * Convenience struct that stores a scalar type and a component count.
 * The compiler can use this to generate SPIR-V types.
 */
struct GcnVectorType {
    GcnScalarType ctype;
    u32 ccount;
};

/**
 * \brief Array type
 *
 * Convenience struct that stores a scalar type, a component count and an array size.
 * An array of length 0 will be evaluated to a vector type.
 * The compiler can use this to generate SPIR-V types.
 */
struct GcnArrayType {
    GcnScalarType ctype;
    u32 ccount;
    u32 alength;
};

/**
 * \brief Register info
 *
 * Stores the array type of a register and its storage class.
 * The compiler can use this to generate SPIR-V pointer types.
 */
struct GcnRegisterInfo {
    GcnArrayType type;
    spv::StorageClass sclass;
};

/**
 * \brief Register value
 *
 * Stores a vector type and a SPIR-V ID that represents an intermediate value.
 * This is used to track the type of such values.
 */
struct GcnRegisterValue {
    GcnVectorType type;
    u32 id;
};

/**
 * \brief Value pair
 *
 * Stores value for a pair of sgpr/vgpr
 * e.g. s[0:1], v[4:5].
 *
 * The high part may be empty/undefined if it's not needed.
 * If type is float16 or float64, we use low to save the value.
 */
struct GcnRegisterValuePair {
    GcnRegisterValue low;
    GcnRegisterValue high;
};

/**
 * \brief Register pointer
 *
 * Stores a vector type and a SPIR-V ID that represents a pointer to such a vector.
 * This can be used to load registers conveniently.
 */
struct GcnRegisterPointer {
    GcnVectorType type;
    u32 id = 0;
};

/**
 * \brief Data shared by all shaders.
 */
struct GcnCompilerCommonPart {
    u32 subgroupSize = 0;
    u32 subgroupInvocationId = 0;
    u32 subgroupEqMask = 0;
    u32 subgroupGeMask = 0;
    u32 subgroupGtMask = 0;
    u32 subgroupLeMask = 0;
    u32 subgroupLtMask = 0;
};

/**
 * \brief Vertex shader-specific structure
 */
struct GcnCompilerVsPart {
    u32 functionId = 0;
    u32 fetchFuncId = 0;

    u32 builtinVertexId = 0;
    u32 builtinInstanceId = 0;
    u32 builtinBaseVertex = 0;
    u32 builtinBaseInstance = 0;
};

/**
 * \brief Pixel shader-specific structure
 */
struct GcnCompilerPsPart {
    u32 functionId = 0;

    u32 builtinFragCoord = 0;
    u32 builtinDepth = 0;
    u32 builtinStencilRef = 0;
    u32 builtinIsFrontFace = 0;
    u32 builtinSampleId = 0;
    u32 builtinSampleMaskIn = 0;
    u32 builtinSampleMaskOut = 0;
    u32 builtinLayer = 0;
    u32 builtinViewportId = 0;

    u32 builtinLaneId = 0;
    u32 killState = 0;

    u32 specRsSampleCount = 0;
};

/**
 * \brief Compute shader-specific structure
 */
struct GcnCompilerCsPart {
    u32 functionId = 0;

    u32 numSubgroups = 0;
    u32 subgroupId = 0;

    u32 workgroupSizeX = 0;
    u32 workgroupSizeY = 0;
    u32 workgroupSizeZ = 0;

    u32 builtinGlobalInvocationId = 0;
    u32 builtinLocalInvocationId = 0;
    u32 builtinLocalInvocationIndex = 0;
    u32 builtinWorkgroupId = 0;

    u32 crossGroupMemoryId = 0;
};

/**
 * \brief Used to index a buffer(V#)
 */
struct GcnRegIndex {
    u32 regIdx = 0;
    GcnInstOperand* relReg = nullptr;
    int32_t offset = 0;
};

/**
 * \brief Component for MIMG instruction vaddr[]
 */
enum class GcnImageAddrComponent : u32 {
    Offsets,
    Bias,
    Zpcf,
    DxDh,
    DyDh,
    DzDh,
    DxDv,
    DyDv,
    DzDv,
    X,
    Y,
    Z,
    Slice,
    FaceId,
    FragId,
    Lod,
    Clamp,
};

/**
 * \brief Image type information from T#
 */
struct GcnImageInfo {
    spv::Dim dim = spv::Dim1D;
    u32 depth = 0;
    u32 array = 0;
    u32 ms = 0;
    u32 sampled = 0;

    vk::ImageViewType vtype = vk::ImageViewType{VK_IMAGE_VIEW_TYPE_MAX_ENUM};
};

struct GcnBufferInfo {
    u32 varId;
    u32 isSsbo;
    GcnBufferMeta buffer;
    GcnImageInfo image;
};

/**
 * \brief Constant buffer binding
 *
 * Stores information for a V#
 */
struct GcnBuffer {
    u32 varId = 0;
    u32 size = 0;
    bool asSsbo = false;
};

/**
 * \brief Shader resource binding
 *
 * Stores information for a T#
 */
struct GcnTexture {
    u32 varId = 0;
    GcnImageInfo imageInfo = {};
    GcnScalarType sampledType = GcnScalarType::Float32;
    u32 sampledTypeId = 0;
    u32 imageTypeId = 0;
    u32 colorTypeId = 0;
    u32 depthTypeId = 0;
};

/**
 * \brief Sampler binding
 *
 * Stores information for a S#
 */
struct GcnSampler {
    u32 varId = 0;
    u32 typeId = 0;
};

enum class GcnCfgBlockType : u32 {
    If,
    Loop,
};

struct GcnCfgBlockIf {
    u32 conditionId;
    u32 labelIf;
    u32 labelElse;
    u32 labelEnd;
    size_t headerPtr;
};

struct GcnCfgBlockLoop {
    u32 labelHeader;
    u32 labelBegin;
    u32 labelContinue;
    u32 labelBreak;
};

struct GcnCfgBlock {
    GcnCfgBlockType type;

    union {
        GcnCfgBlockIf b_if;
        GcnCfgBlockLoop b_loop;
    };
};

/**
 * \brief V# format
 *
 * Used in format buffer load/store
 */
struct GcnBufferFormat {
    u32 sizeInBytes;
    u32 channelCount;
    GcnScalarType channelType;
};

/**
 * \brief SGPR/VGPR array information
 */
struct GcnGprArray {
    u32 arrayId = 0;
    u32 arrayLengthId = 0;
    u32 arrayLength = 0;
};

/**
 * \brief Component swizzle
 *
 * Maps vector components to other vector components.
 */
class GcnRegSwizzle {

public:
    GcnRegSwizzle() {}
    GcnRegSwizzle(u32 x, u32 y, u32 z, u32 w) : m_mask((x << 0) | (y << 2) | (z << 4) | (w << 6)) {}

    u32 operator[](u32 id) const {
        return (m_mask >> (id + id)) & 0x3;
    }

    bool operator==(const GcnRegSwizzle& other) const {
        return m_mask == other.m_mask;
    }
    bool operator!=(const GcnRegSwizzle& other) const {
        return m_mask != other.m_mask;
    }

private:
    u8 m_mask = 0;
};

/**
 * \brief Component mask
 *
 * Enables access to certain subset of vector components.
 */
class GcnRegMask {

public:
    GcnRegMask() = default;
    GcnRegMask(u32 mask) : m_mask(mask) {}
    GcnRegMask(bool x, bool y, bool z, bool w)
        : m_mask((x ? 0x1 : 0) | (y ? 0x2 : 0) | (z ? 0x4 : 0) | (w ? 0x8 : 0)) {}

    bool operator[](u32 id) const {
        return (m_mask >> id) & 1;
    }

    u32 popCount() const {
        constexpr u8 n[16] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
        return n[m_mask & 0xF];
    }

    u32 firstSet() const {
        constexpr u8 n[16] = {4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};
        return n[m_mask & 0xF];
    }

    u32 minComponents() const {
        constexpr u8 n[16] = {0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4};
        return n[m_mask & 0xF];
    }

    bool operator==(const GcnRegMask& other) const {
        return m_mask == other.m_mask;
    }
    bool operator!=(const GcnRegMask& other) const {
        return m_mask != other.m_mask;
    }

    GcnRegMask& operator|=(const GcnRegMask& other) {
        m_mask |= other.m_mask;
        return *this;
    }

    static GcnRegMask firstN(u32 n) {
        return GcnRegMask(n >= 1, n >= 2, n >= 3, n >= 4);
    }

    static GcnRegMask select(u32 n) {
        return GcnRegMask(n == 0, n == 1, n == 2, n == 3);
    }

    std::string maskString() const {
        std::string out = "";
        out += (m_mask & 0x1) ? "x" : "";
        out += (m_mask & 0x2) ? "y" : "";
        out += (m_mask & 0x4) ? "z" : "";
        out += (m_mask & 0x8) ? "w" : "";
        return out;
    }

private:
    u8 m_mask = 0;
};

} // namespace Shader::Gcn
