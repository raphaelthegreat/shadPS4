#pragma once

#include "common/types.h"

namespace Shader::Gcn {

struct CsStageRegisters {
    u32 computePgmLo;
    u32 computePgmHi;
    u32 computePgmRsrc1;
    u32 computePgmRsrc2;
    u32 computeNumThreadX;
    u32 computeNumThreadY;
    u32 computeNumThreadZ;

    void* getCodeAddress() const {
        return (void*)(uintptr_t(computePgmHi) << 40 | uintptr_t(computePgmLo) << 8);
    }
};

struct VsStageRegisters {
    u32 spiShaderPgmLoVs;
    u32 spiShaderPgmHiVs;
    u32 spiShaderPgmRsrc1Vs;
    u32 spiShaderPgmRsrc2Vs;
    u32 spiVsOutConfig;
    u32 spiShaderPosFormat;
    u32 paClVsOutCntl;

    void* getCodeAddress() const {
        return (void*)(uintptr_t(spiShaderPgmHiVs) << 40 | uintptr_t(spiShaderPgmLoVs) << 8);
    }
};

struct PsStageRegisters {
    u32 spiShaderPgmLoPs;
    u32 spiShaderPgmHiPs;
    u32 spiShaderPgmRsrc1Ps;
    u32 spiShaderPgmRsrc2Ps;
    u32 spiShaderZFormat;
    u32 spiShaderColFormat;
    u32 spiPsInputEna;
    u32 spiPsInputAddr;
    u32 spiPsInControl;
    u32 spiBarycCntl;
    u32 dbShaderControl;
    u32 cbShaderMask;

    void* getCodeAddress() const {
        return (void*)(uintptr_t(spiShaderPgmHiPs) << 40 | uintptr_t(spiShaderPgmLoPs) << 8);
    }
};

} // namespace Shader::Gcn
