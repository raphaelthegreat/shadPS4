// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/thread.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

void Scheduler::CommandChunk::ExecuteAll(vk::CommandBuffer cmdbuf) {
    auto command = first;
    while (command != nullptr) {
        auto next = command->GetNext();
        command->Execute(cmdbuf);
        command->~Command();
        command = next;
    }
    submit = false;
    command_offset = 0;
    first = nullptr;
    last = nullptr;
}

Scheduler::Scheduler(const Instance& instance_)
    : instance{instance_},
      queue{instance_, instance_.GetGraphicsQueue(), instance_.GetGraphicsQueueFamilyIndex()},
      command_pool{instance, queue} {
    AllocateWorkerCommandBuffers();
    AcquireNewChunk();
    worker_thread = std::jthread([this](std::stop_token token) { WorkerThread(token); });
}

Scheduler::~Scheduler() = default;

void Scheduler::BeginRendering(const RenderState& new_state) {
    if (is_rendering && render_state == new_state) {
        return;
    }
    Record([state = new_state, is_rendering = is_rendering](vk::CommandBuffer cmdbuf) {
        if (is_rendering) {
            cmdbuf.endRendering();
        }
        const vk::RenderingInfo rendering_info = {
            .renderArea =
                {
                    .offset = {0, 0},
                    .extent = {state.width, state.height},
                },
            .layerCount = state.num_layers,
            .colorAttachmentCount = state.num_color_attachments,
            .pColorAttachments =
                state.num_color_attachments > 0 ? state.color_attachments.data() : nullptr,
            .pDepthAttachment = state.has_depth ? &state.depth_attachment : nullptr,
            .pStencilAttachment = state.has_stencil ? &state.stencil_attachment : nullptr,
        };

        cmdbuf.beginRendering(rendering_info);
    });

    is_rendering = true;
    render_state = new_state;
}

void Scheduler::EndRendering() {
    if (!is_rendering) {
        return;
    }
    Record([](vk::CommandBuffer cmdbuf) { cmdbuf.endRendering(); });
    is_rendering = false;
}

u64 Scheduler::Flush(SubmitInfo& info) {
    // When flushing, we only send data to the driver; no waiting is necessary.
    return SubmitExecution(info);
}

u64 Scheduler::Flush() {
    SubmitInfo info{};
    return Flush(info);
}

void Scheduler::Finish() {
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitInfo info{};
    SubmitExecution(info);
    Wait(presubmit_tick);
}

void Scheduler::WaitWorker() {
    DispatchWork();

    // Ensure the queue is drained.
    {
        std::unique_lock ql{queue_mutex};
        event_cv.wait(ql, [this] { return work_queue.empty(); });
    }

    // Now wait for execution to finish.
    // This needs to be done in the same order as WorkerThread.
    std::scoped_lock el{execution_mutex};
}

void Scheduler::Wait(u64 tick) {
    if (tick >= queue.CurrentTick()) {
        // Make sure we are not waiting for the current tick without signalling
        SubmitInfo info{};
        Flush(info);
    }
    queue.Wait(tick);
}

void Scheduler::DispatchWork() {
    if (chunk->Empty()) {
        return;
    }

    {
        std::scoped_lock ql{queue_mutex};
        work_queue.push(std::move(chunk));
    }

    event_cv.notify_all();
    AcquireNewChunk();
}

void Scheduler::WorkerThread(std::stop_token stop_token) {
    Common::SetCurrentThreadName("VulkanWorker");

    const auto try_pop_queue = [this](auto& work) -> bool {
        if (work_queue.empty()) {
            return false;
        }

        work = std::move(work_queue.front());
        work_queue.pop();
        event_cv.notify_all();
        return true;
    };

    while (!stop_token.stop_requested()) {
        std::unique_ptr<CommandChunk> work;

        {
            std::unique_lock lk{queue_mutex};
            event_cv.wait(lk, stop_token, [&] { return try_pop_queue(work); });

            if (stop_token.stop_requested()) {
                return;
            }

            // Exchange lock ownership so that we take the execution lock before
            // the queue lock goes out of scope. This allows us to force execution
            // to complete in the next step.
            std::exchange(lk, std::unique_lock{execution_mutex});

            const bool has_submit = work->HasSubmit();
            work->ExecuteAll(current_cmdbuf);

            if (has_submit) {
                AllocateWorkerCommandBuffers();
            }
        }

        {
            std::scoped_lock rl{reserve_mutex};
            chunk_reserve.emplace_back(std::move(work));
        }
    }
}

void Scheduler::PopPendingOperations() {
    queue.Refresh();
    while (!pending_ops.empty() && queue.IsFree(pending_ops.front().gpu_tick)) {
        pending_ops.front().callback();
        pending_ops.pop();
    }
}

void Scheduler::AllocateWorkerCommandBuffers() {
    const vk::CommandBufferBeginInfo begin_info = {
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    current_cmdbuf = command_pool.Commit();
    Check(current_cmdbuf.begin(begin_info));
}

u64 Scheduler::SubmitExecution(SubmitInfo& info) {
    EndRendering();
    const u64 signal_value = queue.NextTick();
    info.AddSignal(queue.Semaphore(), signal_value);
    Record([this, info](vk::CommandBuffer cmdbuf) { queue.SubmitNoAdvance(info, cmdbuf); });
    dynamic_state.Invalidate();
    chunk->MarkSubmit();
    DispatchWork();
    PopPendingOperations();
    return signal_value;
}

void Scheduler::AcquireNewChunk() {
    std::scoped_lock lock{reserve_mutex};
    if (chunk_reserve.empty()) {
        chunk = std::make_unique<CommandChunk>();
        return;
    }

    chunk = std::move(chunk_reserve.back());
    chunk_reserve.pop_back();
}

void DynamicState::Commit(const Instance& instance, Scheduler& scheduler) {
    if (dirty_state.viewports) {
        dirty_state.viewports = false;
        scheduler.Record([viewports = viewports](vk::CommandBuffer cmdbuf) {
            cmdbuf.setViewportWithCount(viewports);
        });
    }
    if (dirty_state.scissors) {
        dirty_state.scissors = false;
        scheduler.Record([scissors = scissors](vk::CommandBuffer cmdbuf) {
            cmdbuf.setScissorWithCount(scissors);
        });
    }
    if (dirty_state.depth_test_enabled) {
        dirty_state.depth_test_enabled = false;
        scheduler.Record([depth_test_enabled = depth_test_enabled](vk::CommandBuffer cmdbuf) {
            cmdbuf.setDepthTestEnable(depth_test_enabled);
        });
    }
    if (dirty_state.depth_write_enabled) {
        dirty_state.depth_write_enabled = false;
        // Note that this must be set in a command buffer even if depth test is disabled.
        scheduler.Record([depth_write_enabled = depth_write_enabled](vk::CommandBuffer cmdbuf) {
            cmdbuf.setDepthWriteEnable(depth_write_enabled);
        });
    }
    if (depth_test_enabled && dirty_state.depth_compare_op) {
        dirty_state.depth_compare_op = false;
        scheduler.Record([depth_compare_op = depth_compare_op](vk::CommandBuffer cmdbuf) {
            cmdbuf.setDepthCompareOp(depth_compare_op);
        });
    }
    if (dirty_state.depth_bounds_test_enabled) {
        dirty_state.depth_bounds_test_enabled = false;
        if (instance.IsDepthBoundsSupported()) {
            scheduler.Record(
                [depth_bounds_test_enabled = depth_bounds_test_enabled](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setDepthBoundsTestEnable(depth_bounds_test_enabled);
                });
        }
    }
    if (depth_bounds_test_enabled && dirty_state.depth_bounds) {
        dirty_state.depth_bounds = false;
        if (instance.IsDepthBoundsSupported()) {
            scheduler.Record([depth_bounds_min = depth_bounds_min,
                              depth_bounds_max = depth_bounds_max](vk::CommandBuffer cmdbuf) {
                cmdbuf.setDepthBounds(depth_bounds_min, depth_bounds_max);
            });
        }
    }
    if (dirty_state.depth_bias_enabled) {
        dirty_state.depth_bias_enabled = false;
        scheduler.Record([depth_bias_enabled = depth_bias_enabled](vk::CommandBuffer cmdbuf) {
            cmdbuf.setDepthBiasEnable(depth_bias_enabled);
        });
    }
    if (depth_bias_enabled && dirty_state.depth_bias) {
        dirty_state.depth_bias = false;
        scheduler.Record([depth_bias_constant = depth_bias_constant,
                          depth_bias_clamp = depth_bias_clamp,
                          depth_bias_slope = depth_bias_slope](vk::CommandBuffer cmdbuf) {
            cmdbuf.setDepthBias(depth_bias_constant, depth_bias_clamp, depth_bias_slope);
        });
    }
    if (dirty_state.stencil_test_enabled) {
        dirty_state.stencil_test_enabled = false;
        scheduler.Record([stencil_test_enabled = stencil_test_enabled](vk::CommandBuffer cmdbuf) {
            cmdbuf.setStencilTestEnable(stencil_test_enabled);
        });
    }
    if (stencil_test_enabled) {
        if (dirty_state.stencil_front_ops && dirty_state.stencil_back_ops &&
            stencil_front_ops == stencil_back_ops) {
            dirty_state.stencil_front_ops = false;
            dirty_state.stencil_back_ops = false;
            scheduler.Record([stencil_front_ops = stencil_front_ops](vk::CommandBuffer cmdbuf) {
                cmdbuf.setStencilOp(vk::StencilFaceFlagBits::eFrontAndBack,
                                    stencil_front_ops.fail_op, stencil_front_ops.pass_op,
                                    stencil_front_ops.depth_fail_op, stencil_front_ops.compare_op);
            });
        } else {
            if (dirty_state.stencil_front_ops) {
                dirty_state.stencil_front_ops = false;
                scheduler.Record([stencil_front_ops = stencil_front_ops](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setStencilOp(vk::StencilFaceFlagBits::eFront, stencil_front_ops.fail_op,
                                        stencil_front_ops.pass_op, stencil_front_ops.depth_fail_op,
                                        stencil_front_ops.compare_op);
                });
            }
            if (dirty_state.stencil_back_ops) {
                dirty_state.stencil_back_ops = false;
                scheduler.Record([stencil_back_ops = stencil_back_ops](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setStencilOp(vk::StencilFaceFlagBits::eBack, stencil_back_ops.fail_op,
                                        stencil_back_ops.pass_op, stencil_back_ops.depth_fail_op,
                                        stencil_back_ops.compare_op);
                });
            }
        }
        if (dirty_state.stencil_front_reference && dirty_state.stencil_back_reference &&
            stencil_front_reference == stencil_back_reference) {
            dirty_state.stencil_front_reference = false;
            dirty_state.stencil_back_reference = false;
            scheduler.Record(
                [stencil_front_reference = stencil_front_reference](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack,
                                               stencil_front_reference);
                });
        } else {
            if (dirty_state.stencil_front_reference) {
                dirty_state.stencil_front_reference = false;
                scheduler.Record(
                    [stencil_front_reference = stencil_front_reference](vk::CommandBuffer cmdbuf) {
                        cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eFront,
                                                   stencil_front_reference);
                    });
            }
            if (dirty_state.stencil_back_reference) {
                dirty_state.stencil_back_reference = false;
                scheduler.Record(
                    [stencil_back_reference = stencil_back_reference](vk::CommandBuffer cmdbuf) {
                        cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eBack,
                                                   stencil_back_reference);
                    });
            }
        }
        if (dirty_state.stencil_front_write_mask && dirty_state.stencil_back_write_mask &&
            stencil_front_write_mask == stencil_back_write_mask) {
            dirty_state.stencil_front_write_mask = false;
            dirty_state.stencil_back_write_mask = false;
            scheduler.Record(
                [stencil_front_write_mask = stencil_front_write_mask](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                               stencil_front_write_mask);
                });
        } else {
            if (dirty_state.stencil_front_write_mask) {
                dirty_state.stencil_front_write_mask = false;
                scheduler.Record([stencil_front_write_mask =
                                      stencil_front_write_mask](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFront,
                                               stencil_front_write_mask);
                });
            }
            if (dirty_state.stencil_back_write_mask) {
                dirty_state.stencil_back_write_mask = false;
                scheduler.Record(
                    [stencil_back_write_mask = stencil_back_write_mask](vk::CommandBuffer cmdbuf) {
                        cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eBack,
                                                   stencil_back_write_mask);
                    });
            }
        }
        if (dirty_state.stencil_front_compare_mask && dirty_state.stencil_back_compare_mask &&
            stencil_front_compare_mask == stencil_back_compare_mask) {
            dirty_state.stencil_front_compare_mask = false;
            dirty_state.stencil_back_compare_mask = false;
            scheduler.Record([stencil_front_compare_mask =
                                  stencil_front_compare_mask](vk::CommandBuffer cmdbuf) {
                cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                             stencil_front_compare_mask);
            });
        } else {
            if (dirty_state.stencil_front_compare_mask) {
                dirty_state.stencil_front_compare_mask = false;
                scheduler.Record([stencil_front_compare_mask =
                                      stencil_front_compare_mask](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eFront,
                                                 stencil_front_compare_mask);
                });
            }
            if (dirty_state.stencil_back_compare_mask) {
                dirty_state.stencil_back_compare_mask = false;
                scheduler.Record([stencil_back_compare_mask =
                                      stencil_back_compare_mask](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eBack,
                                                 stencil_back_compare_mask);
                });
            }
        }
    }
    if (dirty_state.primitive_restart_enable) {
        dirty_state.primitive_restart_enable = false;
        if (instance.IsPrimitiveRestartDisableSupported()) {
            scheduler.Record(
                [primitive_restart_enable = primitive_restart_enable](vk::CommandBuffer cmdbuf) {
                    cmdbuf.setPrimitiveRestartEnable(primitive_restart_enable);
                });
        }
    }
    if (dirty_state.rasterizer_discard_enable) {
        dirty_state.rasterizer_discard_enable = false;
        scheduler.Record(
            [rasterizer_discard_enable = rasterizer_discard_enable](vk::CommandBuffer cmdbuf) {
                cmdbuf.setRasterizerDiscardEnable(rasterizer_discard_enable);
            });
    }
    if (dirty_state.cull_mode) {
        dirty_state.cull_mode = false;
        scheduler.Record(
            [cull_mode = cull_mode](vk::CommandBuffer cmdbuf) { cmdbuf.setCullMode(cull_mode); });
    }
    if (dirty_state.front_face) {
        dirty_state.front_face = false;
        scheduler.Record([front_face = front_face](vk::CommandBuffer cmdbuf) {
            cmdbuf.setFrontFace(front_face);
        });
    }
    if (dirty_state.blend_constants) {
        dirty_state.blend_constants = false;
        scheduler.Record([blend_constants = blend_constants](vk::CommandBuffer cmdbuf) {
            cmdbuf.setBlendConstants(blend_constants.data());
        });
    }
    if (dirty_state.color_write_masks) {
        dirty_state.color_write_masks = false;
        if (instance.IsDynamicColorWriteMaskSupported()) {
            scheduler.Record([color_write_masks = color_write_masks](vk::CommandBuffer cmdbuf) {
                cmdbuf.setColorWriteMaskEXT(0, color_write_masks);
            });
        }
    }
    if (dirty_state.line_width) {
        dirty_state.line_width = false;
        scheduler.Record([line_width = line_width](vk::CommandBuffer cmdbuf) {
            cmdbuf.setLineWidth(line_width);
        });
    }
    if (dirty_state.feedback_loop_enabled && instance.IsAttachmentFeedbackLoopLayoutSupported()) {
        dirty_state.feedback_loop_enabled = false;
        scheduler.Record([feedback_loop_enabled = feedback_loop_enabled](vk::CommandBuffer cmdbuf) {
            cmdbuf.setAttachmentFeedbackLoopEnableEXT(feedback_loop_enabled
                                                          ? vk::ImageAspectFlagBits::eColor
                                                          : vk::ImageAspectFlagBits::eNone);
        });
    }
}

} // namespace Vulkan
