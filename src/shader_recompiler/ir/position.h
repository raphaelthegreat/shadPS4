// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/runtime_info.h"

namespace Shader::IR {

/// Maps special position export to builtin attribute stores
inline void ExportPosition(IREmitter& ir, const auto& stage, Attribute attribute, u32 comp,
                           const IR::F32& value) {
    if (attribute == Attribute::Position0) {
        ir.SetAttribute(attribute, value, comp);
        return;
    }
    const u32 index = u32(attribute) - u32(Attribute::Position1);
    const auto output = stage.outputs[index][comp];
    switch (output) {
    case VsOutput::ClipDist0:
    case VsOutput::ClipDist1:
    case VsOutput::ClipDist2:
    case VsOutput::ClipDist3:
    case VsOutput::ClipDist4:
    case VsOutput::ClipDist5:
    case VsOutput::ClipDist6:
    case VsOutput::ClipDist7: {
        const u32 index = u32(output) - u32(VsOutput::ClipDist0);
        ir.SetAttribute(IR::Attribute::ClipDistance, value, index);
        break;
    }
    case VsOutput::CullDist0:
    case VsOutput::CullDist1:
    case VsOutput::CullDist2:
    case VsOutput::CullDist3:
    case VsOutput::CullDist4:
    case VsOutput::CullDist5:
    case VsOutput::CullDist6:
    case VsOutput::CullDist7: {
        const u32 index = u32(output) - u32(VsOutput::CullDist0);
        ir.SetAttribute(IR::Attribute::CullDistance, value, index);
        break;
    }
    case VsOutput::GsMrtIndex:
        ir.SetAttribute(IR::Attribute::RenderTargetId, value);
        break;
    default:
        UNREACHABLE_MSG("Unhandled position attribute {}", u32(output));
    }
}

} // namespace Shader::IR
