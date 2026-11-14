// SPDX-FileCopyrightText: Copyright 2025 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/debug.h"
#include "common/thread.h"
#include "imgui/renderer/texture_manager.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"

namespace Vulkan {

std::mutex Scheduler::submit_mutex;

constexpr u32 MaxQueuedWork = 64;

void Scheduler::CommandChunk::ExecuteAll(vk::CommandBuffer cmdbuf) {
    auto command = first;
    while (command != nullptr) {
        auto next = command->GetNext();
        command->Execute(cmdbuf);
        command->~Command();
        command = next;
    }
    command_offset = 0;
    first = nullptr;
    last = nullptr;
}

Scheduler::Scheduler(const Instance& instance, bool use_worker_thread_)
    : instance{instance}, work_semaphore{instance}, command_pool{instance, &work_semaphore},
      use_worker_thread{use_worker_thread_} {
#if TRACY_GPU_ENABLED
    profiler_scope = reinterpret_cast<tracy::VkCtxScope*>(std::malloc(sizeof(tracy::VkCtxScope)));
#endif
    priority_pending_ops_thread =
        std::jthread(std::bind_front(&Scheduler::PriorityPendingOpsThread, this));
    if (use_worker_thread) {
        AcquireNewChunk();
        worker_thread = std::jthread(std::bind_front(&Scheduler::WorkerThread, this));
    }
}

Scheduler::~Scheduler() {
    if (use_worker_thread) {
        WaitWorker();
        worker_thread.request_stop();
        worker_thread.join();
    }
#if TRACY_GPU_ENABLED
    std::free(profiler_scope);
#endif
}

static vk::RenderingAttachmentInfo MakeColorAttachment(const RenderAttachment& cb) {
    return vk::RenderingAttachmentInfo{
        .imageView = cb.image_view,
        .imageLayout = cb.image_layout,
        .loadOp = cb.is_clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .clearValue = vk::ClearValue{.color = vk::ClearColorValue{.uint32 = cb.clear_value}},
    };
}

void Scheduler::BeginRendering(const RenderState& new_state) {
    if (is_rendering && render_state == new_state) {
        return;
    }
    EndRendering();
    is_rendering = true;
    render_state = new_state;

    Record([state = render_state](vk::CommandBuffer cmdbuf) {
        std::array<vk::RenderingAttachmentInfo, 8> color_attachments;
        for (u32 i = 0; i < state.num_color_attachments; ++i) {
            color_attachments[i] = MakeColorAttachment(state.color_attachments[i]);
        }

        const auto& db = state.depth_stencil_attachment;
        const vk::RenderingAttachmentInfo depth_attachment = {
            .imageView = db.image_view,
            .imageLayout = db.image_layout,
            .loadOp = db.depth_clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = vk::ClearValue{.depthStencil =
                                             vk::ClearDepthStencilValue{
                                                 .depth = std::bit_cast<float>(db.clear_value[0])}},
        };
        const vk::RenderingAttachmentInfo stencil_attachment = {
            .imageView = db.image_view,
            .imageLayout = db.image_layout,
            .loadOp = db.stencil_clear ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue =
                vk::ClearValue{.depthStencil =
                                   vk::ClearDepthStencilValue{.stencil = db.clear_value[1]}},
        };

        cmdbuf.beginRendering(vk::RenderingInfo{
            .renderArea =
                {
                    .offset = {0, 0},
                    .extent = {state.width, state.height},
                },
            .layerCount = state.num_layers,
            .colorAttachmentCount = state.num_color_attachments,
            .pColorAttachments = color_attachments.data(),
            .pDepthAttachment = db.has_depth ? &depth_attachment : nullptr,
            .pStencilAttachment = db.has_stencil ? &stencil_attachment : nullptr,
        });
    });
}

void Scheduler::EndRendering() {
    if (!is_rendering) {
        return;
    }
    if (on_end_rendering) {
        on_end_rendering();
    }
    is_rendering = false;
    Record([](vk::CommandBuffer cmdbuf) { cmdbuf.endRendering(); });
}

void Scheduler::Flush(SubmitInfo& info) {
    // When flushing, we only send data to the worker thread; no waiting is necessary.
    SubmitExecution(info);
}

void Scheduler::Flush() {
    SubmitInfo info{};
    Flush(info);
}

void Scheduler::Finish() {
    // When finishing, we need to wait for the submission to have executed on the device.
    const u64 presubmit_tick = CurrentTick();
    SubmitInfo info{};
    SubmitExecution(info);
    Wait(presubmit_tick);
}

void Scheduler::Wait(u64 tick) {
    if (tick >= work_semaphore.CurrentTick()) {
        // Make sure we are not waiting for the current tick without signalling
        SubmitInfo info{};
        Flush(info);
    }
    work_semaphore.Wait(tick);
}

void Scheduler::WaitWorker() {
    if (!use_worker_thread) {
        return;
    }
    DispatchWork();
    std::unique_lock lk{work_mutex};
    work_done_cv.wait(lk, [this] { return num_pending_work == 0; });
}

void Scheduler::DispatchWork() {
    if (!use_worker_thread || (chunk->Empty() && !chunk->HasSubmit())) {
        return;
    }
    {
        std::unique_lock lk{work_mutex};
        work_done_cv.wait(lk, [this] { return work_queue.size() < MaxQueuedWork; });
        work_queue.push(std::move(chunk));
        ++num_pending_work;
    }
    work_cv.notify_one();
    ++current_chunk_id;
    AcquireNewChunk();
}

void Scheduler::WaitChunk(u64 id) {
    if (!use_worker_thread) {
        return;
    }
    ASSERT(id <= current_chunk_id);
    if (id == current_chunk_id) {
        DispatchWork();
    }
    u64 executed = executed_chunk_id.load(std::memory_order_acquire);
    while (executed < id) {
        executed_chunk_id.wait(executed, std::memory_order_acquire);
        executed = executed_chunk_id.load(std::memory_order_acquire);
    }
}

void Scheduler::AcquireNewChunk() {
    std::scoped_lock lk{reserve_mutex};
    if (chunk_reserve.empty()) {
        chunk = std::make_unique<CommandChunk>();
        return;
    }
    chunk = std::move(chunk_reserve.back());
    chunk_reserve.pop_back();
}

void Scheduler::PopPendingOperations() {
    if (pending_ops_empty.load(std::memory_order_relaxed)) {
        return;
    }
    std::unique_lock lk(pending_ops_mutex);
    work_semaphore.Refresh();
    while (!pending_ops.empty() && work_semaphore.IsFree(pending_ops.front().gpu_tick)) {
        pending_ops.front().callback();
        pending_ops.pop();
    }
    pending_ops_empty.store(pending_ops.empty(), std::memory_order_relaxed);
}

vk::CommandBuffer Scheduler::GetCommandBuffer() {
    if (current_cmdbuf) {
        return current_cmdbuf;
    }
    current_cmdbuf = command_pool.Commit();
    Check(current_cmdbuf.begin(vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    }));

#if TRACY_GPU_ENABLED
    auto* profiler_ctx = instance.GetProfilerContext();
    if (profiler_ctx) {
        static const auto scope_loc =
            GPU_SCOPE_LOCATION("Guest Frame", MarkersPalette::GpuMarkerColor);
        new (profiler_scope) tracy::VkCtxScope{profiler_ctx, &scope_loc, current_cmdbuf, true};
    }
#endif
    return current_cmdbuf;
}

void Scheduler::SubmitCommandBuffer(SubmitInfo& info) {
    const vk::CommandBuffer cmdbuf = GetCommandBuffer();

#if TRACY_GPU_ENABLED
    auto* profiler_ctx = instance.GetProfilerContext();
    if (profiler_ctx) {
        profiler_scope->~VkCtxScope();
        TracyVkCollect(profiler_ctx, cmdbuf);
    }
#endif

    Check(cmdbuf.end());
    current_cmdbuf = vk::CommandBuffer{};

    std::array<vk::PipelineStageFlags, std::tuple_size_v<decltype(info.wait_semas)>>
        wait_stage_masks;
    wait_stage_masks.fill(vk::PipelineStageFlagBits::eAllCommands);

    const vk::TimelineSemaphoreSubmitInfo timeline_si = {
        .waitSemaphoreValueCount = info.num_wait_semas,
        .pWaitSemaphoreValues = info.wait_ticks.data(),
        .signalSemaphoreValueCount = info.num_signal_semas,
        .pSignalSemaphoreValues = info.signal_ticks.data(),
    };

    const vk::SubmitInfo submit_info = {
        .pNext = &timeline_si,
        .waitSemaphoreCount = info.num_wait_semas,
        .pWaitSemaphores = info.wait_semas.data(),
        .pWaitDstStageMask = wait_stage_masks.data(),
        .commandBufferCount = 1u,
        .pCommandBuffers = &cmdbuf,
        .signalSemaphoreCount = info.num_signal_semas,
        .pSignalSemaphores = info.signal_semas.data(),
    };

    std::scoped_lock lk{submit_mutex};
    ImGui::Core::TextureManager::Submit();
    auto submit_result = instance.GetGraphicsQueue().submit(submit_info, info.fence);
    ASSERT_MSG(submit_result != vk::Result::eErrorDeviceLost, "Device lost during submit");
}

void Scheduler::SubmitExecution(SubmitInfo& info) {
    EndRendering();
    if (on_submit) {
        on_submit(info);
    }

    const u64 signal_value = work_semaphore.NextTick();
    info.AddSignal(work_semaphore.Handle(), signal_value);

    if (use_worker_thread) {
        chunk->MarkSubmit(info);
        DispatchWork();
    } else {
        SubmitCommandBuffer(info);
    }

    // Invalidate dynamic state so it gets applied to the new command buffer.
    dynamic_state.Invalidate();

    // Apply pending operations
    PopPendingOperations();
}

void Scheduler::WorkerThread(std::stop_token stoken) {
    Common::SetCurrentThreadName("shadPS4:VkWorker");
    Common::SetCurrentThreadPriority(Common::ThreadPriority::High);

    while (true) {
        std::unique_ptr<CommandChunk> work;
        {
            std::unique_lock lk{work_mutex};
            work_cv.wait(lk, stoken, [this] { return !work_queue.empty(); });
            if (work_queue.empty()) {
                break;
            }
            work = std::move(work_queue.front());
            work_queue.pop();
        }

        if (!work->Empty()) {
            work->ExecuteAll(GetCommandBuffer());
        }
        executed_chunk_id.fetch_add(1, std::memory_order_release);
        executed_chunk_id.notify_all();
        if (work->HasSubmit()) {
            SubmitCommandBuffer(work->GetSubmitInfo());
            work->Reset();
        }

        {
            std::scoped_lock lk{reserve_mutex};
            chunk_reserve.push_back(std::move(work));
        }
        {
            std::scoped_lock lk{work_mutex};
            --num_pending_work;
        }
        work_done_cv.notify_all();
    }
}

void Scheduler::PriorityPendingOpsThread(std::stop_token stoken) {
    Common::SetCurrentThreadName("shadPS4:GpuSchedPriorityPendingOpsRunner");

    while (!stoken.stop_requested()) {
        PendingOp op;
        {
            std::unique_lock lk(priority_pending_ops_mutex);
            priority_pending_ops_cv.wait(lk, stoken,
                                         [this] { return !priority_pending_ops.empty(); });
            if (stoken.stop_requested()) {
                break;
            }

            op = std::move(priority_pending_ops.front());
            priority_pending_ops.pop();
        }

        work_semaphore.Wait(op.gpu_tick);
        if (stoken.stop_requested()) {
            break;
        }

        op.callback();
    }
}

void DynamicState::Commit(const Instance& instance, Scheduler& scheduler) {
    DirtyState emit = dirty_state;
    if (!depth_test_enabled) {
        emit.depth_compare_op = false;
    }
    if (!depth_bounds_test_enabled) {
        emit.depth_bounds = false;
    }
    if (!depth_bias_enabled) {
        emit.depth_bias = false;
    }
    if (!stencil_test_enabled) {
        emit.stencil_front_ops = false;
        emit.stencil_front_reference = false;
        emit.stencil_front_write_mask = false;
        emit.stencil_front_compare_mask = false;
        emit.stencil_back_ops = false;
        emit.stencil_back_reference = false;
        emit.stencil_back_write_mask = false;
        emit.stencil_back_compare_mask = false;
    }
    if (!instance.IsAttachmentFeedbackLoopLayoutSupported()) {
        emit.feedback_loop_enabled = false;
    }

    static_assert(sizeof(DirtyState) == sizeof(u32));
    const u32 emit_bits = std::bit_cast<u32>(emit);
    if (emit_bits == 0) {
        return;
    }
    dirty_state = std::bit_cast<DirtyState>(std::bit_cast<u32>(dirty_state) & ~emit_bits);

    scheduler.Record([&instance, state = *this, emit](vk::CommandBuffer cmdbuf) {
        state.Emit(instance, emit, cmdbuf);
    });
}

void DynamicState::Emit(const Instance& instance, DirtyState emit, vk::CommandBuffer cmdbuf) const {
    if (emit.viewports) {
        cmdbuf.setViewportWithCount(viewports);
    }
    if (emit.scissors) {
        cmdbuf.setScissorWithCount(scissors);
    }
    if (emit.depth_test_enabled) {
        cmdbuf.setDepthTestEnable(depth_test_enabled);
    }
    if (emit.depth_write_enabled) {
        // Note that this must be set in a command buffer even if depth test is disabled.
        cmdbuf.setDepthWriteEnable(depth_write_enabled);
    }
    if (depth_test_enabled && emit.depth_compare_op) {
        cmdbuf.setDepthCompareOp(depth_compare_op);
    }
    if (emit.depth_bounds_test_enabled) {
        if (instance.IsDepthBoundsSupported()) {
            cmdbuf.setDepthBoundsTestEnable(depth_bounds_test_enabled);
        }
    }
    if (depth_bounds_test_enabled && emit.depth_bounds) {
        if (instance.IsDepthBoundsSupported()) {
            cmdbuf.setDepthBounds(depth_bounds_min, depth_bounds_max);
        }
    }
    if (emit.depth_bias_enabled) {
        cmdbuf.setDepthBiasEnable(depth_bias_enabled);
    }
    if (depth_bias_enabled && emit.depth_bias) {
        cmdbuf.setDepthBias(depth_bias_constant, depth_bias_clamp, depth_bias_slope);
    }
    if (emit.stencil_test_enabled) {
        cmdbuf.setStencilTestEnable(stencil_test_enabled);
    }
    if (stencil_test_enabled) {
        if (emit.stencil_front_ops && emit.stencil_back_ops &&
            stencil_front_ops == stencil_back_ops) {
            cmdbuf.setStencilOp(vk::StencilFaceFlagBits::eFrontAndBack, stencil_front_ops.fail_op,
                                stencil_front_ops.pass_op, stencil_front_ops.depth_fail_op,
                                stencil_front_ops.compare_op);
        } else {
            if (emit.stencil_front_ops) {
                cmdbuf.setStencilOp(vk::StencilFaceFlagBits::eFront, stencil_front_ops.fail_op,
                                    stencil_front_ops.pass_op, stencil_front_ops.depth_fail_op,
                                    stencil_front_ops.compare_op);
            }
            if (emit.stencil_back_ops) {
                cmdbuf.setStencilOp(vk::StencilFaceFlagBits::eBack, stencil_back_ops.fail_op,
                                    stencil_back_ops.pass_op, stencil_back_ops.depth_fail_op,
                                    stencil_back_ops.compare_op);
            }
        }
        if (emit.stencil_front_reference && emit.stencil_back_reference &&
            stencil_front_reference == stencil_back_reference) {
            cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eFrontAndBack,
                                       stencil_front_reference);
        } else {
            if (emit.stencil_front_reference) {
                cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eFront,
                                           stencil_front_reference);
            }
            if (emit.stencil_back_reference) {
                cmdbuf.setStencilReference(vk::StencilFaceFlagBits::eBack, stencil_back_reference);
            }
        }
        if (emit.stencil_front_write_mask && emit.stencil_back_write_mask &&
            stencil_front_write_mask == stencil_back_write_mask) {
            cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                       stencil_front_write_mask);
        } else {
            if (emit.stencil_front_write_mask) {
                cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eFront,
                                           stencil_front_write_mask);
            }
            if (emit.stencil_back_write_mask) {
                cmdbuf.setStencilWriteMask(vk::StencilFaceFlagBits::eBack, stencil_back_write_mask);
            }
        }
        if (emit.stencil_front_compare_mask && emit.stencil_back_compare_mask &&
            stencil_front_compare_mask == stencil_back_compare_mask) {
            cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eFrontAndBack,
                                         stencil_front_compare_mask);
        } else {
            if (emit.stencil_front_compare_mask) {
                cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eFront,
                                             stencil_front_compare_mask);
            }
            if (emit.stencil_back_compare_mask) {
                cmdbuf.setStencilCompareMask(vk::StencilFaceFlagBits::eBack,
                                             stencil_back_compare_mask);
            }
        }
    }
    if (emit.primitive_restart_enable) {
        cmdbuf.setPrimitiveRestartEnable(primitive_restart_enable);
    }
    if (emit.rasterizer_discard_enable) {
        cmdbuf.setRasterizerDiscardEnable(rasterizer_discard_enable);
    }
    if (emit.cull_mode) {
        cmdbuf.setCullMode(cull_mode);
    }
    if (emit.front_face) {
        cmdbuf.setFrontFace(front_face);
    }
    if (emit.blend_constants) {
        cmdbuf.setBlendConstants(blend_constants.data());
    }
    if (emit.color_write_masks) {
        if (instance.IsDynamicColorWriteMaskSupported()) {
            cmdbuf.setColorWriteMaskEXT(0, color_write_masks);
        }
    }
    if (emit.line_width) {
        cmdbuf.setLineWidth(line_width);
    }
    if (emit.feedback_loop_enabled && instance.IsAttachmentFeedbackLoopLayoutSupported()) {
        cmdbuf.setAttachmentFeedbackLoopEnableEXT(feedback_loop_enabled
                                                      ? vk::ImageAspectFlagBits::eColor
                                                      : vk::ImageAspectFlagBits::eNone);
    }
}

} // namespace Vulkan
