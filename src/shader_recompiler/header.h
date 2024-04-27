#pragma once

#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "shader_recompiler/shader_binary.h"
#include "shader_recompiler/shader_key.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Shader::Gcn {

enum class GcnProgramType : u16;

/**
 * \brief Represent a resource bound to a GCN shader
 */
struct ShaderResource {
    vk::DescriptorType type;

    // ShaderInputUsageType
    u32 usage;

    // SGPR index
    u32 startRegister;

    // Resource is in User Data Register or in Extended User Data region.
    bool inEud;

    // Dword offset in EUD
    u32 eudOffsetInDwords;

    // Register size in dwords
    u32 sizeInDwords;

    // Is the image going to be sampled
    bool isSampled;
};

using ShaderResourceTable = std::vector<ShaderResource>;

/**
 * \brief Light weight binary information parser
 *
 * Data in this class is not persistent, will become invalid when shader code released.
 */
class BinaryInfo {
public:
    BinaryInfo(const void* shaderCode);
    ~BinaryInfo();

    /**
     * \brief Code length
     *
     * Gcn instruction code length in bytes. Does not include header and other meta information
     */
    u32 length() const {
        return m_binInfo->m_length;
    }

    /**
     * \brief Unique key
     *
     * Used to identify the shader
     */
    ShaderKey key() const {
        return ShaderKey(m_binInfo->m_shaderHash0, m_binInfo->m_crc32);
    }

    /**
     * \brief Shader stage
     */
    ShaderBinaryType stage() const;

    /**
     * \brief Information of shader binary
     */
    const ShaderBinaryInfo* info() const {
        return m_binInfo;
    }

private:
    const ShaderBinaryInfo* m_binInfo = nullptr;
};

/**
 * \brief Header information
 *
 * Stores header for a shader binary sent to graphics driver.
 */
class Header {
    using ResourceTypeInfo = std::unordered_map<u32, bool>;

public:
    explicit Header(const u8* shaderCode);
    ~Header();

    GcnProgramType type() const;

    /**
     * \brief Unique id of the shader
     */
    ShaderKey key() const {
        return ShaderKey(m_binInfo.m_shaderHash0, m_binInfo.m_crc32);
    }

    /**
     * \brief Shader code size in bytes
     */
    u32 length() const {
        return m_binInfo.m_length;
    }

    const InputUsageSlotTable& getInputUsageSlotTable() const {
        return m_inputUsageSlotTable;
    }

    const ShaderResourceTable& getShaderResourceTable() const {
        return m_resourceTable;
    }

private:
    void parseHeader(const u8* shaderCode);

    void extractResourceTable(const u8* code);

    ResourceTypeInfo analyzeResourceType(const u8* code);

private:
    ShaderBinaryInfo m_binInfo = {};
    InputUsageSlotTable m_inputUsageSlotTable;
    ShaderResourceTable m_resourceTable;
};

} // namespace Shader::Gcn
