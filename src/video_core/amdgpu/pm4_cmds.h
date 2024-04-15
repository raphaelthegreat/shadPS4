// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstring>
#include "common/bit_field.h"
#include "common/types.h"
#include "video_core/amdgpu/pm4_opcodes.h"

namespace AmdGpu {

// PM4 command shifts
#define PM4_PREDICATE_SHIFT              0
#define PM4_SHADERTYPE_SHIFT             1
#define PM4_OP_SHIFT                     8
#define PM4_COUNT_SHIFT                  16
#define PM4_TYPE_SHIFT                   30
#define PM4_T0_ONE_REG_WR_SHIFT          15
#define PM4_T0_INDX_SHIFT                0

// PM4 command control settings
#define PM4_T0_NO_INCR                  (1 << PM4_T0_ONE_REG_WR_SHIFT)

// ROLL_CONTEXT defines
#define PM4_SEL_8_CP_STATE               0
#define PM4_SEL_BLOCK_STATE              1

/// This enum defines the Shader types supported in PM4 type 3 header
enum class PM4ShaderType : u32 {
    ShaderGraphics  = 0,    ///< Graphics shader
    ShaderCompute   = 1     ///< Compute shader
};

/// This enum defines the predicate value supported in PM4 type 3 header
enum class PM4Predicate : u32 {
    PredDisable = 0,    ///< Predicate disabled
    PredEnable  = 1     ///< Predicate enabled
};

// PM4 type 3 header macro for creating a PM4 type 3 header
#define PM4_TYPE_3_HDR(opCode, count, shaderType, predicate)             \
      ((unsigned int)(predicate      <<  PM4_PREDICATE_SHIFT)    |       \
                     (shaderType     <<  PM4_SHADERTYPE_SHIFT)   |       \
                     (PM4_TYPE_3     <<  PM4_TYPE_SHIFT)         |       \
                     ((count - 2)    <<  PM4_COUNT_SHIFT)        |       \
                     (opCode         <<  PM4_OP_SHIFT))

// PM4 type 0 header macros
#define PM4_TYPE_0_HDR(Reg0, nWrites)                                    \
    ((((unsigned int)(nWrites)-1) << PM4_COUNT_SHIFT)       |            \
                     ((Reg0)      << PM4_T0_INDX_SHIFT))

// RJVR: This macro needs to be modified to use Type 3 ONE_REG_WRITE.
#define PM4_TYPE_0_HDR_NO_INCR(Reg0, nWrites)                            \
    ((((unsigned int)(nWrites)-1) << PM4_COUNT_SHIFT) |                  \
     ((Reg0) << PM4_T0_INDX_SHIFT)                    |                  \
     PM4_T0_NO_INCR)

// PM4 type 2 NOP
#define PM4_TYPE_2_NOP                   (PM4_TYPE_2 << PM4_TYPE_SHIFT)

union PM4Type0Header {
    u32 raw;
    BitField<0, 16, u32> base;      ///< DWORD Memory-mapped address
    BitField<16, 14, u32> count;    ///< Count of DWORDs in the *information* body (N - 1 for N dwords)
    BitField<30, 2, u32> type;      ///< Packet identifier. It should be 0 for type 0 packets.
};

union PM4Type3Header {
    constexpr PM4Type3Header(PM4ItOpcode code, u32 num_words_min_one,
                             PM4ShaderType stype = PM4ShaderType::ShaderGraphics,
                             PM4Predicate pred = PM4Predicate::PredDisable) {
        raw = 0;
        predicate.Assign(pred);
        shaderType.Assign(stype);
        opcode.Assign(code);
        count.Assign(num_words_min_one);
        type.Assign(3);
    }

    u32 raw;
    BitField<0, 1, PM4Predicate> predicate;     ///< Predicated version of packet when set
    BitField<1, 1, PM4ShaderType> shaderType;   ///< 0: Graphics, 1: Compute Shader
    BitField<8, 8, PM4ItOpcode> opcode;         ///< IT opcode
    BitField<16, 14, u32> count;                ///< Number of DWORDs - 1 in the information body.
    BitField<30, 2, u32> type;                  ///< Packet identifier. It should be 3 for type 3 packets
};

union PM4Header {
    u32 raw;
    PM4Type0Header type0;
    PM4Type3Header type3;
    BitField<30, 2, u32> type;
};

template <PM4ItOpcode opcode, typename... Args>
constexpr u32* Write(u32* cmdbuf, PM4ShaderType type, Args... data) {
    // Write the PM4 header.
    PM4Type3Header header{opcode, sizeof...(Args) - 1, type};
    std::memcpy(cmdbuf, &header, sizeof(header));

    // Write arguments
    const std::array<u32, sizeof...(Args)> args{data...};
    std::memcpy(++cmdbuf, args.data(), sizeof(args));
    cmdbuf += args.size();
    return cmdbuf;
}

union ContextControlEnable {
    u32 raw;
    BitField<0, 1, u32> enableSingleCntxConfigReg;  ///< single context config reg
    BitField<1, 1, u32> enableMultiCntxRenderReg;   ///< multi context render state reg
    BitField<15, 1, u32> enableUserConfigReg__CI;   ///< User Config Reg on CI(reserved for SI)
    BitField<16, 1, u32> enableGfxSHReg;            ///< Gfx SH Registers
    BitField<24, 1, u32> enableCSSHReg;             ///< CS SH Registers
    BitField<31, 1, u32> enableDw;                  ///< DW enable
};

struct PM4CmdContextControl {
    PM4Type3Header header;
    ContextControlEnable loadControl;   ///< Enable bits for loading
    ContextControlEnable shadowEnable;  ///< Enable bits for shadowing
};

union LoadAddressHigh {
    u32 raw;
    BitField<0, 16, u32> addrHi;    ///< bits for the block in Memory from where the CP will fetch the state
    BitField<31, 1, u32> waitIdle;  ///< if set the CP will wait for the graphics pipe to be idle by writing
                                    ///< to the GRBM Wait Until register with "Wait for 3D idle"
};

/**
 * PM4CMDLOADDATA can be used with the following opcodes
 * - IT_LOAD_CONFIG_REG
 * - IT_LOAD_CONTEXT_REG
 * - IT_LOAD_SH_REG
 */
struct PM4CmdLoadData {
    PM4Type3Header header;
    u32 addrLo;     ///< low 32 address bits for the block in memory from where the CP will fetch the state
    LoadAddressHigh addrHi;
    u32 regOffset;  ///< offset in DWords from the register base address
    u32 numDwords;  ///< number of DWords that the CP will fetch and write into the chip. A value of zero will fetch nothing
};

enum class LoadDataIndex : u32 {
    DirectAddress = 0,  /// ADDR_LO is direct address
    Offset = 1,         /// ARRD_LO is ignored and memory offset is in addrOffset
};

enum class LoadDataFormat : u32 {
    OffsetAndSize = 0,  /// Data is consecutive DWORDs
    OffsetAndData = 1,  /// Register offset and data is interleaved
};

union LoadAddressLow {
    u32 raw;
    BitField<0, 1, LoadDataIndex> index;
    BitField<2, 30, u32> addrLo;    ///< bits for the block in Memory from where the CP will fetch the state. DWORD aligned
};

/**
 * PM4CMDLOADDATAINDEX can be used with the following opcodes (VI+)
 * - IT_LOAD_CONTEXT_REG_INDEX
 * - IT_LOAD_SH_REG_INDEX
 */
struct PM4CmdLoadDataIndex {
    PM4Type3Header header;
    LoadAddressLow addrLo;  ///< low 32 address bits for the block in memory from where the CP will fetch the state
    u32 addrOffset;         ///< addrLo.index = 1 Indexed mode
    union {
        BitField<0, 16, u32> regOffset;   ///< offset in DWords from the register base address
        BitField<31, 1, LoadDataFormat> dataFormat;
        u32 raw;
    };
    u32 numDwords;  ///< Number of DWords that the CP will fetch and write
                    ///< into the chip. A value of zero will fetch nothing
};


// SET_CONTEXT_REG index values (CI+)
#define SET_CONTEXT_INDEX_DEFAULT             0  // Use this for all registers except the following...
#define SET_CONTEXT_INDEX_MULTI_VGT_PARAM     1  // Use this when writing IA_MULTI_VGT_PARAM
#define SET_CONTEXT_INDEX_VGT_LS_HS_CONFIG    2  // Use this when writing VGT_LS_HS_CONFIG
#define SET_CONTEXT_INDEX_PA_SC_RASTER_CONFIG 3  // Use this when writing PA_SC_RASTER_CONFIG

#define SET_CONTEXT_INDEX_SHIFT 28 // Offset in ordinal2 of the index field.

// SET_UCONFIG_REG index values (CI+)
#define SET_UCONFIG_INDEX_DEFAULT       0  // Use this for all registers except the following...
#define SET_UCONFIG_INDEX_PRIM_TYPE     1  // Use this when writing VGT_PRIMITIVE_TYPE
#define SET_UCONFIG_INDEX_INDEX_TYPE    2  // Use this when writing VGT_INDEX_TYPE
#define SET_UCONFIG_INDEX_NUM_INSTANCES 3  // Use this when writing VGT_NUM_INSTANCES

// SET_SH_REG_INDEX index values (Hawaii, VI+)
// Index (0-2): reserved
#define SET_SH_REG_INDEX_CP_MODIFY_CU_MASK 3 // Use this to modify CU_EN for COMPUTE_STATIC* and SPI_SHADER_PGM_RSRC3*
                                             // CP performs AND operation on KMD and UMD CU masks to write registers.

/**
 * PM4CMDSETDATA can be used with the following opcodes:
 *
 * - IT_SET_CONFIG_REG
 * - IT_SET_CONTEXT_REG
 * - IT_SET_CONTEXT_REG_INDIRECT
 * - IT_SET_SH_REG
 * - IT_SET_SH_REG_INDEX
 * - IT_SET_UCONFIG_REG
 */
struct PM4CmdSetData {
    PM4Type3Header header;
    union {
        u32 raw;
        BitField<0, 16, u32> regOffset; ///< Offset in DWords from the register base address
        BitField<28, 4, u32> index;     ///< Index for UCONFIG/CONTEXT on CI+
                                        ///< Program to zero for other opcodes and on SI
    };

    template <PM4ShaderType type = PM4ShaderType::ShaderGraphics, typename... Args>
    static constexpr u32* SetContextReg(u32* cmdbuf, Args... data) {
        return Write<PM4ItOpcode::SetContextReg>(cmdbuf, type, data...);
    }

    template <PM4ShaderType type = PM4ShaderType::ShaderGraphics, typename... Args>
    static constexpr u32* SetShReg(u32* cmdbuf, Args... data) {
        return Write<PM4ItOpcode::SetShReg>(cmdbuf, type, data...);
    }
};

struct PM4CmdNop {
    PM4Type3Header header;
};

struct PM4CmdDrawIndexOffset2 {
    PM4Type3Header header;
    u32 maxSize;        ///< Maximum number of indices
    u32 indexOffset;    ///< Zero based starting index number in the index buffer
    u32 indexCount;     ///< number of indices in the Index Buffer
    u32 drawInitiator;  ///< draw Initiator Register
};

struct PM4CmdDrawIndex2 {
    PM4Type3Header header;
    u32 maxSize;        ///< maximum number of indices
    u32 indexBaseLo;    ///< base Address Lo [31:1] of Index Buffer
                        ///< (Word-Aligned). Written to the VGT_DMA_BASE register.
    u32 indexBaseHi;    ///< base Address Hi [39:32] of Index Buffer.
                        ///< Written to the VGT_DMA_BASE_HI register
    u32 indexCount;     ///< number of indices in the Index Buffer.
                        ///< Written to the VGT_NUM_INDICES register.
    u32 drawInitiator;  ///< written to the VGT_DRAW_INITIATOR register
};

} // namespace AmdGpu
