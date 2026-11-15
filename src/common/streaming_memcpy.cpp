// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstdint>
#include <cstring>

#ifdef __SSE4_1__
#include <immintrin.h>
#endif

#include "common/streaming_memcpy.h"

namespace Common {

void StreamingLoadMemcpy(u8* dst, const u8* src, u64 size) {
#ifdef __SSE4_1__
    const u64 misalign = reinterpret_cast<uintptr_t>(src) & 15;
    const u64 head = std::min<u64>(misalign ? 16 - misalign : 0, size);
    std::memcpy(dst, src, head);
    dst += head;
    src += head;
    size -= head;
    _mm_mfence();
    for (; size >= 64; size -= 64, src += 64, dst += 64) {
        auto* src_vec = reinterpret_cast<__m128i*>(const_cast<u8*>(src));
        auto* dst_vec = reinterpret_cast<__m128i*>(dst);
        const __m128i a = _mm_stream_load_si128(src_vec);
        const __m128i b = _mm_stream_load_si128(src_vec + 1);
        const __m128i c = _mm_stream_load_si128(src_vec + 2);
        const __m128i d = _mm_stream_load_si128(src_vec + 3);
        _mm_storeu_si128(dst_vec, a);
        _mm_storeu_si128(dst_vec + 1, b);
        _mm_storeu_si128(dst_vec + 2, c);
        _mm_storeu_si128(dst_vec + 3, d);
    }
    for (; size >= 16; size -= 16, src += 16, dst += 16) {
        const __m128i a = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<u8*>(src)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst), a);
    }
#endif
    std::memcpy(dst, src, size);
}

} // namespace Common
