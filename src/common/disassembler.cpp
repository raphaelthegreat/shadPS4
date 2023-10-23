#include <array>
#include <fmt/core.h>

#include "common/disassembler.h"

namespace Common {

Disassembler::Disassembler() {
	ZydisDecoderInit(&m_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
	ZydisFormatterInit(&m_formatter, ZYDIS_FORMATTER_STYLE_INTEL);
}

Disassembler::~Disassembler() = default;

void Disassembler::printInstruction(void* code, u64 address) {
    ZydisDecodedInstruction instruction{};
    ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT_VISIBLE];
    ZyanStatus status = ZydisDecoderDecodeFull(&m_decoder, code, sizeof(code), &instruction, operands);

    if (!ZYAN_SUCCESS(status)) [[unlikely]] {
        fmt::print("Decode instruction failed at %p\n", code);
        return;
    }

    printInst(instruction, operands, address);
}

void Disassembler::printInst(ZydisDecodedInstruction& inst, ZydisDecodedOperand* operands, u64 address) {
    std::array<char, 256> sz_buffer;
    ZydisFormatterFormatInstruction(&m_formatter, &inst, operands,inst.operand_count_visible,
                                    sz_buffer.data(), sizeof(sz_buffer), address, ZYAN_NULL);
    fmt::print("instruction: {}\n", sz_buffer.data());
}

} // namespace Common
