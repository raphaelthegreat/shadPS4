// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <limits>
#include "common/logging/classes.h"
#pragma clang optimize off
#include <unordered_map>
#include "shader_recompiler/frontend/control_flow_graph.h"
#include "shader_recompiler/frontend/decode.h"
#include "shader_recompiler/frontend/structured_control_flow.h"
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/ir/opcodes.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/post_order.h"
#include "shader_recompiler/ir/reg.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"

namespace Shader {

IR::BlockList GenerateBlocks(const IR::AbstractSyntaxList& syntax_list) {
    size_t num_syntax_blocks{};
    for (const auto& [_, type] : syntax_list) {
        if (type == IR::AbstractSyntaxNode::Type::Block) {
            ++num_syntax_blocks;
        }
    }
    IR::BlockList blocks{};
    blocks.reserve(num_syntax_blocks);
    for (const auto& [data, type] : syntax_list) {
        if (type == IR::AbstractSyntaxNode::Type::Block) {
            blocks.push_back(data.block);
        }
    }
    return blocks;
}

void EmitControlFlowGraph(IR::Program& program, Pools& pools, Gcn::CFG& cfg,
                          RuntimeInfo& runtime_info, const Profile& profile) {
    Gcn::Translator translator{program.info, runtime_info, profile};
    bool emit_prologue = true;
    for (auto& block : cfg) {
        const u32 start = block.begin_index;
        const u32 size = block.end_index - start + 1;
        auto* ir_block = pools.block_pool.Create(pools.inst_pool);
        ir_block->cfg_block = &block;
        block.ir_block = ir_block;
        translator.Translate(ir_block, block.begin,
                             std::span{program.ins_list}.subspan(start, size));
        if (emit_prologue) {
            translator.EmitPrologue(ir_block);
            emit_prologue = false;
        }
        program.blocks.push_back(ir_block);
    }
    ASSERT_MSG(!program.info.translation_failed, "Shader translation has failed");
    for (auto& block : cfg) {
        auto* ir_block = block.ir_block;
        if (block.branch_true) {
            auto* true_block = block.branch_true->ir_block;
            ir_block->AddBranch(true_block);
        }
        if (block.branch_false) {
            auto* false_block = block.branch_false->ir_block;
            ir_block->AddBranch(false_block);
        }
    }
    program.post_order_blocks = Shader::IR::PostOrder(program.blocks.front());
}

void LowerLdsSpillsToRegistersPass(IR::Program& program, const RuntimeInfo& runtime_info) {
    if (program.info.stage != Stage::Compute) {
        return;
    }

    struct DsInst {
        IR::Inst* inst;
        u32 offset;
    };
    boost::container::small_vector<DsInst, 16> ds_insts;
    for (IR::Block* const block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::WriteSharedU32 &&
                inst.GetOpcode() != IR::Opcode::LoadSharedU32) {
                continue;
            }

            u32 offset{};
            IR::Value addr = inst.Arg(0);
            if (auto* prod = addr.TryInst(); prod && prod->GetOpcode() == IR::Opcode::IAdd32) {
                if (!prod->Arg(1).IsImmediate()) {
                    continue;
                }
                offset = prod->Arg(1).U32();
                addr = prod->Arg(0);
            }
            if (addr.IsImmediate()) {
                continue;
            }
            IR::Inst* prod = addr.Inst();
            if (prod->GetOpcode() != IR::Opcode::ShiftLeftLogical32 || prod->Arg(0).IsImmediate() ||
                !prod->Arg(1).IsImmediate() ||prod->Arg(1).U32() != 2) {
                continue;
            }
            IR::Inst* lane_id = prod->Arg(0).Inst();
            if (lane_id->GetOpcode() != IR::Opcode::LaneId) {
                continue;
            }

            ds_insts.emplace_back(&inst, offset);
        }
    }

    if (ds_insts.empty()) {
        return;
    }

    LOG_CRITICAL(Render_Recompiler, "SPILL PATCH TRIGGER FOR {:#x}", program.info.pgm_hash);
    std::unordered_map<u32, IR::VirtualReg> reg_map;
    for (auto [inst, offset] : ds_insts) {
        auto [it, new_reg] = reg_map.try_emplace(offset);
        if (new_reg) {
            it->second = IR::VirtualReg{program.next_reg_index++, IR::Type::U32};
        }
        IR::IREmitter ir{*inst->GetParent(), IR::Block::InstructionList::s_iterator_to(*inst)};
        if (inst->GetOpcode() == IR::Opcode::LoadSharedU32) {
            inst->ReplaceUsesWithAndRemove(ir.GetVirtualReg(it->second));
        } else {
            ir.SetVirtualReg(it->second, IR::U32{inst->Arg(1)});
            inst->Invalidate();
        }
    }

    for (auto* ir_block : program.blocks) {
        ir_block->ssa_state.Reset();
    }
    Shader::Optimization::SsaRewritePass(program);
}

static bool IsCompositeExtract(IR::Inst* const inst) {
    switch (inst->GetOpcode()) {
    case IR::Opcode::CompositeExtractU32x2:
    case IR::Opcode::CompositeExtractU32x3:
    case IR::Opcode::CompositeExtractU32x4:
        return true;
    default:
        return false;
    }
}

static IR::Inst* FoldPhi(IR::Inst& phi, IR::Opcode opcode, IR::Type type, auto&&... args) {
    auto insert_point = IR::Block::InstructionList::s_iterator_to(phi);
    IR::Block* block = phi.GetParent();
    IR::Inst* const new_phi{&*block->PrependNewInst(insert_point, IR::Opcode::Phi)};
    new_phi->SetFlags(type);

    for (size_t arg_index = 0; arg_index < phi.NumArgs(); ++arg_index) {
        new_phi->AddPhiOperand(phi.PhiBlock(arg_index), phi.Arg(arg_index).Inst()->Arg(0));
    }

    // Insert folded opcode after block phis
    auto it = std::ranges::find_if_not(block->Instructions(), IR::IsPhi);
    IR::Value const replacement{&*block->PrependNewInst(it, opcode,
                                                        {IR::Value{new_phi}, IR::Value{args}...})};
    phi.ReplaceUsesWithAndRemove(replacement);
    ASSERT(!insert_point->HasUses());
    block->Instructions().erase(insert_point);
    return new_phi;
}

static IR::Inst* FoldPhiArgOpIntoPhi(IR::Inst& phi) {
    IR::Inst* const first_arg = phi.Arg(0).Inst();
    const IR::Opcode opcode = first_arg->GetOpcode();
    const IR::Type phi_type = first_arg->Arg(0).Type();

    if (IsCompositeExtract(first_arg)) {
        const u32 index = first_arg->Arg(1).U32();
        for (size_t arg_index = 1; arg_index < phi.NumArgs(); ++arg_index) {
            const IR::Inst* arg = phi.Arg(arg_index).Inst();
            if (arg->Arg(1).U32() != index) {
                return nullptr;
            }
        }
        return FoldPhi(phi, opcode, phi_type, index);
    } else if (first_arg->NumArgs() == 1) {
        return FoldPhi(phi, opcode, phi_type);
    }

    return nullptr;
}

static bool AllPhiArgsHaveSameOp(IR::Inst& phi) {
    IR::Inst* const first_arg = phi.Arg(0).Inst();
    bool same_op = true;
    for (size_t arg_index = 1; arg_index < phi.NumArgs(); ++arg_index) {
        if (phi.Arg(arg_index).IsImmediate()) {
            return false;
        }
        IR::Inst* arg = phi.Arg(arg_index).Inst();
        if (first_arg->GetOpcode() != arg->GetOpcode()) {
            same_op = false;
            break;
        }
    }
    return same_op;
}

static IR::Inst* VisitPhiNode(IR::Inst& phi) {
    if (phi.Arg(0).IsImmediate()) {
        return nullptr;
    }

    // If all phi operands are the same operation, pull them through the phi
    if (AllPhiArgsHaveSameOp(phi)) {
        if (IR::Inst* inst = FoldPhiArgOpIntoPhi(phi)) {
            return inst;
        }
    }

    // If there are identical phi nodes in the current block, deduplicate them
    IR::Block* block = phi.GetParent();
    for (IR::Inst& inst : block->Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::Phi) {
            break;
        }
        if (&inst == &phi) {
            continue;
        }
        bool identical = true;
        for (size_t i = 0; i < inst.NumArgs(); i++) {
            if (phi.PhiBlock(i) != inst.PhiBlock(i) || phi.Arg(i) != inst.Arg(i)) {
                identical = false;
                break;
            }
        }
        if (identical) {
            phi.ReplaceUsesWithAndRemove(IR::Value{&inst});
            auto it = IR::Block::InstructionList::s_iterator_to(phi);
            ASSERT(!it->HasUses());
            block->Instructions().erase(it);
            return &inst;
        }
    }

    return nullptr;
}

void PhiSimplificationPass(IR::Program& program) {
    std::vector<IR::Inst*> worklist;
    for (IR::Block* const block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::Phi) {
                break;
            }
            worklist.push_back(&inst);
        }
    }
    while (!worklist.empty()) {
        IR::Inst* phi = worklist.back();
        worklist.pop_back();
        if (auto* new_phi = VisitPhiNode(*phi)) {
            worklist.push_back(new_phi);
        }
    }
}

void InverseBallotEliminationPass(IR::Program& program) {
    std::vector<IR::Inst*> worklist;
    for (IR::Block* const block : program.blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::InverseBallot) {
                continue;
            }
            worklist.push_back(&inst);
        }
    }
    std::unordered_map<IR::Inst*, IR::Value> bitwise_to_logical_map;
    while (!worklist.empty()) {
        IR::Inst* const inst = worklist.back();
        worklist.pop_back();

        if (inst->GetOpcode() == IR::Opcode::Void) {
            continue;
        }

        IR::Value value{inst->Arg(0)};
        if (value.IsImmediate()) {
            if (value.U64() == 0ull) {
                inst->ReplaceUsesWithAndRemove(IR::Value{false});
            } else if (value.U64() == std::numeric_limits<u64>::max()) {
                inst->ReplaceUsesWithAndRemove(IR::Value{true});
            } else {
                UNREACHABLE_MSG("Unexpected immediate argument for InverseBallot {:#x}", value.U64());
            }
            continue;
        }

        IR::Inst* const prod = value.Inst();
        if (prod->GetOpcode() == IR::Opcode::Ballot) {
            inst->ReplaceUsesWithAndRemove(prod->Arg(0));
            continue;
        }


        if (prod->GetOpcode() != IR::Opcode::BitwiseAnd64 &&
            prod->GetOpcode() != IR::Opcode::BitwiseNot64 &&
            prod->GetOpcode() != IR::Opcode::BitwiseOr64 &&
            prod->GetOpcode() != IR::Opcode::BitwiseXor64 &&
            prod->GetOpcode() != IR::Opcode::SelectU64 &&
            prod->GetOpcode() != IR::Opcode::Phi) {
            continue;
        }

        auto [it, is_new] = bitwise_to_logical_map.try_emplace(prod);
        if (is_new) {
            IR::Block* const block = prod->GetParent();
            auto insert_point = IR::Block::InstructionList::s_iterator_to(*prod);
            IR::IREmitter ir{*block, insert_point};

            if (prod->GetOpcode() == IR::Opcode::Phi) {
                IR::Inst* const new_phi{&*block->PrependNewInst(insert_point, IR::Opcode::Phi)};
                new_phi->SetFlags(IR::Type::U1);
                for (size_t arg_index = 0; arg_index < prod->NumArgs(); ++arg_index) {
                    IR::Block* const phi_block = prod->PhiBlock(arg_index);
                    IR::IREmitter ir{*phi_block};
                    const IR::U1 new_arg = ir.InverseBallot(IR::U64{prod->Arg(arg_index)});
                    worklist.push_back(new_arg.Inst());
                    new_phi->AddPhiOperand(phi_block, new_arg);
                }
                it->second = IR::Value{new_phi};
            } else if (prod->GetOpcode() == IR::Opcode::SelectU64) {
                const IR::U1 a = ir.InverseBallot(IR::U64{prod->Arg(1)});
                worklist.push_back(a.Inst());
                const IR::U1 b = ir.InverseBallot(IR::U64{prod->Arg(2)});
                worklist.push_back(b.Inst());
                it->second = ir.Select(IR::U1{prod->Arg(0)}, a, b);
            } else {
                const IR::U1 a = ir.InverseBallot(IR::U64{prod->Arg(0)});
                worklist.push_back(a.Inst());

                if (prod->GetOpcode() == IR::Opcode::BitwiseNot64) {
                    it->second = ir.LogicalNot(a);
                } else {
                    const IR::U1 b = ir.InverseBallot(IR::U64{prod->Arg(1)});
                    worklist.push_back(b.Inst());
                    switch (prod->GetOpcode()) {
                    case IR::Opcode::BitwiseAnd64:
                        it->second = ir.LogicalAnd(a, b);
                        break;
                    case IR::Opcode::BitwiseOr64:
                        it->second = ir.LogicalOr(a, b);
                        break;
                    case IR::Opcode::BitwiseXor64:
                        it->second = ir.LogicalXor(a, b);
                        break;
                    default:
                        break;
                    }
                }
            }
        }

        auto uses = prod->Uses();
        for (auto [user, operand] : uses) {
            if (user->GetOpcode() == IR::Opcode::InverseBallot) {
                user->ReplaceUsesWithAndRemove(it->second);
            }
        }
    }
}

IR::Program TranslateProgram(const std::span<const u32>& code, Pools& pools, Info& info,
                             RuntimeInfo& runtime_info, const Profile& profile) {
    // Ensure first instruction is expected.
    constexpr u32 token_mov_vcchi = 0xBEEB03FF;
    if (code[0] != token_mov_vcchi) {
        LOG_WARNING(Render_Recompiler, "First instruction is not s_mov_b32 vcc_hi, #imm");
    }

    Gcn::GcnCodeSlice slice(code.data(), code.data() + code.size());
    Gcn::GcnDecodeContext decoder;

    // Decode and save instructions
    IR::Program program{info};
    program.ins_list.reserve(code.size());
    while (!slice.atEnd()) {
        program.ins_list.emplace_back(decoder.decodeInstruction(slice));
    }

    // Clear any previous pooled data.
    pools.ReleaseContents();

    // Create control flow graph
    Common::ObjectPool<Gcn::Block> gcn_block_pool{64};
    Gcn::CFG cfg{gcn_block_pool, program.ins_list};
    EmitControlFlowGraph(program, pools, cfg, runtime_info, profile);

    // On NVIDIA GPUs HW interpolation of clip distance values seems broken, and we need to emulate
    // it with expensive discard in PS.
    Shader::InjectClipDistanceAttributes(program, runtime_info);

    // Run optimization passes on unstructured graph
    if (!profile.support_float64) {
        Shader::Optimization::LowerFp64ToFp32(program);
    }
    Shader::Optimization::SsaRewritePass(program);
    Shader::Optimization::ConstantPropagationPass(program.post_order_blocks);
    Shader::IR::DumpProgram(program, info, "post-ssa1.");
    if (program.info.pgm_hash == 0x41d379bc) {
        printf("Bad\n");
    }
    LowerLdsSpillsToRegistersPass(program, runtime_info);
    if (info.l_stage == LogicalStage::TessellationControl) {
        Shader::Optimization::TessellationPreprocess(program, runtime_info);
        Shader::Optimization::HullShaderTransform(program, runtime_info);
    } else if (info.l_stage == LogicalStage::TessellationEval) {
        Shader::Optimization::TessellationPreprocess(program, runtime_info);
        Shader::Optimization::DomainShaderTransform(program, runtime_info);
    }
    Shader::Optimization::RingAccessElimination(program, runtime_info);
    Shader::Optimization::ReadLaneEliminationPass(program);
    auto resources = Shader::Optimization::ResourceDiscoverPass(program, profile);
    Shader::Optimization::FlattenExtendedUserdataPass(program);
    Shader::Optimization::ResourcePatchingPass(program.info, resources, profile);
    Shader::Optimization::LowerBufferFormatToRaw(program);
    Shader::Optimization::SharedMemorySimplifyPass(program, profile);
    Shader::Optimization::SharedMemoryToStoragePass(program, runtime_info, profile);
    Shader::Optimization::LowerUserClipPlanes(program, runtime_info);
    Shader::IR::DumpProgram(program, info, "pre-lower-phi.");

    // Prepare for structurization by clearing flow graph and lowering phis
    for (auto* ir_block : program.blocks) {
        ir_block->imm_predecessors.clear();
        ir_block->imm_successors.clear();
        ir_block->ssa_state.Reset();
    }
    Shader::Optimization::LowerPhisToRegsPass(program);

    // Structurize control flow graph and create program.
    program.syntax_list = Shader::Gcn::BuildASL(pools, cfg, info);
    program.blocks = GenerateBlocks(program.syntax_list);
    program.post_order_blocks = Shader::IR::PostOrder(program.syntax_list.front().data.block);

    // Run optimization passes on structured graph
    Shader::IR::DumpProgram(program, info, "pre-repair.");
    Shader::Optimization::SsaRepairPass(program);
    Shader::IR::DumpProgram(program, info, "post-repair.");
    Shader::Optimization::SsaRewritePass(program);
    Shader::IR::DumpProgram(program, info, "post-ssa2.");
    PhiSimplificationPass(program);
    Shader::Optimization::ConstantPropagationPass(program.post_order_blocks);
    InverseBallotEliminationPass(program);
    Shader::Optimization::DeadCodeEliminationPass(program);
    Shader::Optimization::SharedMemoryBarrierPass(program, runtime_info, profile);
    Shader::Optimization::CollectShaderInfoPass(program, profile);
    Shader::IR::DumpProgram(program, info);

    return program;
}

} // namespace Shader
