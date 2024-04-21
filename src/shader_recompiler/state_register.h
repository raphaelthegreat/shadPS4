#pragma once

#include "common/types.h"
#include "shader_recompiler/compiler_defs.h"

namespace Shader::Gcn {
class GcnCompiler;

/**
 * \brief Abstracts a 64 bits hardware state register
 */
class GcnStateRegister {
public:
    GcnStateRegister(GcnCompiler* compiler, const char* name);

    ~GcnStateRegister();

    /**
     * \brief Set initial value.
     */
    void init(u32 lowId, u32 highId);

    /**
     * \brief Load value and return the result
     *
     * The mask can be used to specify result width,
     * either one gpr or two gpr.
     * e.g:
     * mask == 1 -> s0
     * mask == 3 -> s[0:1]
     *
     * Note when mask == 2,
     * the result will be put in low part.
     */
    GcnRegisterValuePair emitLoad(const GcnRegMask& mask);

    /**
     * \brief Store value.
     *
     * The mask can be used to specify value width,
     * either one gpr or two gpr.
     * e.g:
     * mask == 1 -> s0
     * mask == 3 -> s[0:1]
     *
     * Note when mask == 2, the low part of the value
     * will be stored to high part of register.
     */
    void emitStore(const GcnRegisterValuePair& value, const GcnRegMask& mask);

    /**
     * \brief Zero status
     *
     * Return a spir-v boolean type value id
     * indicating the hardware register's zero state.
     * (vccz or execz)
     * The state is auto updated whenever a store
     * operation is performed.
     */
    u32 zflag();

private:
    void create();

    void updateZflag();

private:
    GcnCompiler* m_compiler;
    std::string m_name;
    bool m_created = false;
    GcnRegisterPointer m_low = {};
    GcnRegisterPointer m_high = {};
    u32 m_zflag = 0;
};

/**
 * \brief Hardware state registers
 *
 * Will be updated after specific instruction execution.
 */
struct GcnStateRegisters {
    GcnStateRegister exec;
    GcnStateRegister vcc;

    GcnRegisterPointer m0;
    GcnRegisterPointer scc;
};
} // namespace Shader::Gcn
