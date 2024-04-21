#pragma once

#include "common/types.h"
#include "shader_recompiler/spirv/GLSL.std.450.hpp"
#include "shader_recompiler/spirv/spirv.hpp"

namespace Shader::Gcn {

/**
 * \brief SPIR-V instruction
 *
 * Helps parsing a single instruction, providing access to the op code, instruction length and
 * instruction arguments.
 */
class SpirvInstruction {
public:
    SpirvInstruction() = default;
    SpirvInstruction(u32* code, u32 offset, u32 length)
        : m_code(code), m_offset(offset), m_length(length) {}

    /**
     * \brief SPIR-V Op code
     * \returns The op code
     */
    spv::Op opCode() const {
        return static_cast<spv::Op>(this->arg(0) & spv::OpCodeMask);
    }

    /**
     * \brief Instruction length
     * \returns Number of DWORDs
     */
    u32 length() const {
        return this->arg(0) >> spv::WordCountShift;
    }

    /**
     * \brief Instruction offset
     * \returns Offset in DWORDs
     */
    u32 offset() const {
        return m_offset;
    }

    /**
     * \brief Argument value
     *
     * Retrieves an argument DWORD. Note that some instructions
     * take 64-bit arguments which require more than one DWORD.
     * Arguments start at index 1. Calling this method with an
     * argument ID of 0 will return the opcode token.
     * \param [in] idx Argument index, starting at 1
     * \returns The argument value
     */
    u32 arg(u32 idx) const {
        const u32 index = m_offset + idx;
        return index < m_length ? m_code[index] : 0;
    }

    /**
     * \brief Argument string
     *
     * Retrieves a pointer to a UTF-8-encoded string.
     * \param [in] idx Argument index, starting at 1
     * \returns Pointer to the literal string
     */
    const char* chr(u32 idx) const {
        const u32 index = m_offset + idx;
        return index < m_length ? reinterpret_cast<const char*>(&m_code[index]) : nullptr;
    }

    /**
     * \brief Changes the value of an argument
     *
     * \param [in] idx Argument index, starting at 1
     * \param [in] word New argument word
     */
    void setArg(u32 idx, u32 word) const {
        if (m_offset + idx < m_length)
            m_code[m_offset + idx] = word;
    }

private:
    u32* m_code = nullptr;
    u32 m_offset = 0;
    u32 m_length = 0;
};

/**
 * \brief SPIR-V instruction iterator
 *
 * Convenient iterator that can be used
 * to process raw SPIR-V shader code.
 */
class SpirvInstructionIterator {

public:
    SpirvInstructionIterator() {}
    SpirvInstructionIterator(u32* code, u32 offset, u32 length)
        : m_code(length != 0 ? code : nullptr), m_offset(length != 0 ? offset : 0),
          m_length(length) {
        if ((length >= 5) && (m_code[0] == spv::MagicNumber))
            this->advance(5);
    }

    SpirvInstructionIterator& operator++() {
        this->advance(SpirvInstruction(m_code, m_offset, m_length).length());
        return *this;
    }

    SpirvInstruction operator*() const {
        return SpirvInstruction(m_code, m_offset, m_length);
    }

    bool operator==(const SpirvInstructionIterator& other) const {
        return this->m_code == other.m_code && this->m_offset == other.m_offset &&
               this->m_length == other.m_length;
    }

    bool operator!=(const SpirvInstructionIterator& other) const {
        return this->m_code != other.m_code || this->m_offset != other.m_offset ||
               this->m_length != other.m_length;
    }

private:
    u32* m_code = nullptr;
    u32 m_offset = 0;
    u32 m_length = 0;

    void advance(u32 n) {
        if (m_offset + n < m_length) {
            m_offset += n;
        } else {
            m_code = nullptr;
            m_offset = 0;
            m_length = 0;
        }
    }
};

} // namespace Shader::Gcn
