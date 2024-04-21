#pragma once

#include "common/types.h"

namespace Shader::Gcn {

struct GcnOptions {
    // A GCN wavefront has 64 lanes, but the host GPU may not fit the case,
    // especially for Nvidia GPU, of which a warp has 32 lanes,
    // in such case we need to take smaller subgroup size into consideration,
    // and separate subgroups while compiling.
    bool separateSubgroup;
};

/**
 * \brief Tessellation info
 *
 * Stores the maximum tessellation factor
 * to export from tessellation shaders.
 */
struct GcnTessInfo {
    float maxTessFactor;
};

/**
 * \brief Xfb capture entry
 *
 * Stores an output variable to capture,
 * as well as the buffer to write it to.
 */
struct GcnXfbEntry {
    const char* semanticName;
    u32 semanticIndex;
    u32 componentIndex;
    u32 componentCount;
    u32 streamId;
    u32 bufferId;
    u32 offset;
};

/**
 * \brief Xfb info
 *
 * Stores capture entries and output buffer
 * strides. This structure must only be
 * defined if \c entryCount is non-zero.
 */
struct GcnXfbInfo {
    u32 entryCount;
    GcnXfbEntry entries[128];
    u32 strides[4];
    int32_t rasterizedStream;
};

/**
 * \brief Shader module info
 *
 * Stores information which may affect shader compilation.
 * This data can be supplied by the client API implementation.
 */
struct GcnModuleInfo {
    GcnOptions options;
    // Maximum subgroup count in a single local workgroup
    // equal to maxComputeWorkGroupInvocations / subgroupSize
    u32 maxComputeSubgroupCount;
};
} // namespace Shader::Gcn
