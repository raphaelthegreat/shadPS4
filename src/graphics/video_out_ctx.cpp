#include "common/debug.h"
#include "core/hle/libraries/libkernel/libkernel.h"
#include "graphics/video_out_ctx.h"

namespace Core::Libraries {

void VideoOutCtx::Init(u32 width, u32 height) {
    m_video_out_ctx.m_resolution.fullWidth = width;
    m_video_out_ctx.m_resolution.fullHeight = height;
    m_video_out_ctx.m_resolution.paneWidth = width;
    m_video_out_ctx.m_resolution.paneHeight = height;
}
int VideoOutCtx::Open() {
    std::scoped_lock lock{m_mutex};

    int handle = -1;

    if (!m_video_out_ctx.isOpened) {
        handle = 1;  // positive return , should be more than 1 ?
    }

    m_video_out_ctx.isOpened = true;
    m_video_out_ctx.m_flip_status = SceVideoOutFlipStatus();
    m_video_out_ctx.m_flip_status.flipArg = -1;
    m_video_out_ctx.m_flip_status.currentBuffer = -1;
    m_video_out_ctx.m_flip_status.count = 0;
    m_video_out_ctx.m_vblank_status = SceVideoOutVblankStatus();

    return handle;
}
void VideoOutCtx::Close(s32 handle) {
    std::scoped_lock lock{m_mutex}; 

    m_video_out_ctx.isOpened = false;

    if (m_video_out_ctx.m_flip_evtEq.size() > 0)
    {
        BREAKPOINT(); //we need to clear all events if they have been created
    }
    
    m_video_out_ctx.m_flip_rate = 0;

    // clear buffers
    for (auto& buffer : m_video_out_ctx.buffers) {
        buffer.buffer = nullptr;
        buffer.buffer_render = nullptr;
        buffer.buffer_size = 0;
        buffer.set_id = 0;
    }

    m_video_out_ctx.buffers_sets.clear();

    m_video_out_ctx.buffers_registration_index = 0;
}

VideoConfigInternal* VideoOutCtx::getCtx(int handle) {
    if (handle != 1) [[unlikely]] {
        return nullptr;
    }
    return &m_video_out_ctx;  // assuming that it's the only ctx TODO check if we need more
}

void FlipQueue::getFlipStatus(VideoConfigInternal* cfg, SceVideoOutFlipStatus* out) {
    std::scoped_lock lock(m_mutex);
    *out = cfg->m_flip_status;
}

bool FlipQueue::submitFlip(VideoConfigInternal* cfg, s32 index, s64 flip_arg) {
    std::scoped_lock lock{m_mutex};

    if (m_requests.size() >= 2) {
        return false;
    }

    Request r{};
    r.cfg = cfg;
    r.index = index;
    r.flip_arg = flip_arg;
    r.submit_tsc = HLE::Libs::LibKernel::sceKernelReadTsc();

    m_requests.push_back(r);

    cfg->m_flip_status.flipPendingNum = static_cast<int>(m_requests.size());
    cfg->m_flip_status.gcQueueNum = 0;

    m_submit_cond.notify_one();

    return true;
}

bool FlipQueue::flip(u32 micros) {
    Request* request;
    {
        std::unique_lock lock{m_mutex};
        m_submit_cond.wait_for(lock, std::chrono::microseconds(micros),
                               [&] { return !m_requests.empty(); });
        if (m_requests.empty()) {
            return false;
        }
        request = &m_requests.at(0);  // Process first request
    }

    const auto buffer = request->cfg->buffers[request->index].buffer_render;
    Emu::DrawBuffer(buffer);

    std::scoped_lock lock{m_mutex};

    {
        std::scoped_lock cfg_lock{request->cfg->m_mutex};
        for (auto& flip_eq : request->cfg->m_flip_evtEq) {
            if (flip_eq != nullptr) {
                flip_eq->triggerEvent(SCE_VIDEO_OUT_EVENT_FLIP, Kernel::EVFILT_VIDEO_OUT,
                                      reinterpret_cast<void*>(request->flip_arg));
            }
        }
    }

    m_requests.erase(m_requests.begin());
    m_done_cond.notify_one();

    request->cfg->m_flip_status.count++;
    //TODO request.cfg->m_flip_status.processTime = LibKernel::KernelGetProcessTime();
    request->cfg->m_flip_status.tsc = sceKernelReadTsc();
    request->cfg->m_flip_status.submitTsc = request->submit_tsc;
    request->cfg->m_flip_status.flipArg = request->flip_arg;
    request->cfg->m_flip_status.currentBuffer = request->index;
    request->cfg->m_flip_status.flipPendingNum = static_cast<int>(m_requests.size());

    return false; 
}

}; // namespace Core::Libraries
