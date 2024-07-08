// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include "common/assert.h"
#include "common/alignment.h"
#include "common/error.h"
#include "video_core/memory_tracker.h"

#ifndef _WIN64
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#endif

namespace VideoCore {

constexpr size_t PAGESIZE = 4_KB;
constexpr size_t PAGEBITS = 12;

struct MemoryTracker::Impl {
    Impl() {
        uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
        ASSERT_MSG(uffd != -1, "{}", Common::GetLastErrorMsg());

        // Request uffdio features from kernel.
        uffdio_api uffdio_api;
        uffdio_api.api = UFFD_API;
        uffdio_api.features = 0;
        const int ret = ioctl(uffd, UFFDIO_API, &uffdio_api);
        ASSERT(ret == 0 && uffdio_api.api == UFFD_API);

        // Create uffd handler thread
        ufd_thread = std::jthread([&](std::stop_token token) { UffdHandler(token); });
    }

    void OnMap(VAddr address, size_t size) {
        uffdio_register uffdio_register;
        uffdio_register.range.start = address;
        uffdio_register.range.len = size;
        uffdio_register.mode = UFFDIO_REGISTER_MODE_WP;
        const int ret = ioctl(uffd, UFFDIO_REGISTER, &uffdio_register);
        ASSERT_MSG(ret != -1, "Uffdio register failed");
    }

    void OnUnmap(VAddr address, size_t size) {
        uffdio_range uffdio_range;
        uffdio_range.start = address;
        uffdio_range.len = size;
        const int ret = ioctl(uffd, UFFDIO_UNREGISTER, &uffdio_range);
        ASSERT_MSG(ret != -1, "Uffdio unregister failed");
    }

    void UffdHandler(std::stop_token token) {
        while (!token.stop_requested()) {
            pollfd pollfd;
            pollfd.fd = uffd;
            pollfd.events = POLLIN;

            // Block until the descriptor is ready for data reads.
            const int pollres = poll(&pollfd, 1, -1);
            switch (pollres) {
            case -1:
                perror("Poll userfaultfd");
                continue;
                break;
            case 0:
                continue;
            case 1:
                break;
            default:
                UNREACHABLE_MSG("Unexpected number of descriptors {} out of poll", pollres);
            }

            // We don't want an error condition to have occured.
            ASSERT_MSG(!(pollfd.revents & POLLERR), "POLLERR on userfaultfd");

            // We waited until there is data to read, we don't care about anything else.
            if (!(pollfd.revents & POLLIN)) {
                continue;
            }

            // Read message from kernel.
            uffd_msg msg;
            const int readret = read(uffd, &msg, sizeof(msg));
            ASSERT_MSG(readret != -1 || errno == EAGAIN, "Unexpected result of uffd read");
            if (errno == EAGAIN) {
                continue;
            }
            ASSERT_MSG(readret == sizeof(msg), "Unexpected short read, exiting");

            const VAddr addr = msg.arg.pagefault.address;
            const VAddr addr_page = Common::AlignDown(addr, PAGESIZE);
            ASSERT(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP);

            // Remove protection on this page.
            struct uffdio_writeprotect wp;
            wp.range.start = addr_page;
            wp.range.len = PAGESIZE;
            wp.mode = 0;
            const int ret = ioctl(uffd, UFFDIO_WRITEPROTECT, &wp);
            ASSERT_MSG(ret != -1, "Ioctl UFFDIO_WRITEPROTECT failed");
        }
    }

    std::jthread ufd_thread;
    int uffd;
};

MemoryTracker::MemoryTracker() {}

MemoryTracker::~MemoryTracker() {}

} // namespace VideoCore
