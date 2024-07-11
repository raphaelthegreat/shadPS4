// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <queue>
#include <mutex>
#include "common/assert.h"
#include "common/singleton.h"
#include "common/logging/log.h"
#include "core/libraries/avplayer/avplayer.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "core/file_sys/fs.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace Libraries::AvPlayer {

typedef void* (*SceAvPlayerAllocate)(void* argP, uint32_t argAlignment, uint32_t argSize);
typedef void (*SceAvPlayerDeallocate)(void* argP, void* argMemory);
typedef void* (*SceAvPlayerAllocateTexture)(void* argP, uint32_t argAlignment, uint32_t argSize);
typedef void (*SceAvPlayerDeallocateTexture)(void* argP, void* argMemory);

typedef struct SceAvPlayerMemAllocator {
    void* objectPointer;
    SceAvPlayerAllocate allocate;
    SceAvPlayerDeallocate deallocate;
    SceAvPlayerAllocateTexture allocateTexture;
    SceAvPlayerDeallocateTexture deallocateTexture;
} SceAvPlayerMemAllocator;

typedef int (*SceAvPlayerOpenFile)(void* argP, const char* argFilename);
typedef int (*SceAvPlayerCloseFile)(void* argP);
typedef int (*SceAvPlayerReadOffsetFile)(void* argP, uint8_t* argBuffer, uint64_t argPosition, uint32_t argLength);
typedef uint64_t (*SceAvPlayerSizeFile)(void* argP);

typedef struct SceAvPlayerFileReplacement {
    void* objectPointer;
    SceAvPlayerOpenFile open;
    SceAvPlayerCloseFile close;
    SceAvPlayerReadOffsetFile readOffset;
    SceAvPlayerSizeFile size;
} SceAvPlayerFileReplacement;

typedef void (*SceAvPlayerEventCallback)(void* p, int32_t argEventId, int32_t argSourceId, void* argEventData);

typedef struct SceAvPlayerEventReplacement {
    void* objectPointer;
    SceAvPlayerEventCallback eventCallback;
} SceAvPlayerEventReplacement;

typedef enum SceAvPlayerDebuglevels {
    SCE_AVPLAYER_DBG_NONE,
    SCE_AVPLAYER_DBG_INFO,
    SCE_AVPLAYER_DBG_WARNINGS,
    SCE_AVPLAYER_DBG_ALL
} SceAvPlayerDebuglevels;

typedef enum SceAvPlayerStreamType {
    SCE_AVPLAYER_VIDEO,
    SCE_AVPLAYER_AUDIO,
    SCE_AVPLAYER_TIMEDTEXT,
    SCE_AVPLAYER_UNKNOWN
} SceAvPlayerStreamType;

typedef struct SceAvPlayerInitData {
    SceAvPlayerMemAllocator		memoryReplacement;
    SceAvPlayerFileReplacement	fileReplacement;
    SceAvPlayerEventReplacement		eventReplacement;
    SceAvPlayerDebuglevels		debugLevel;
    uint32_t	basePriority;
    int32_t		numOutputVideoFrameBuffers;
    bool		autoStart;
    uint8_t		reserved[3];
    const char*	defaultLanguage;
} SceAvPlayerInitData;

typedef enum SceAvPlayerEvents {
    SCE_AVPLAYER_STATE_STOP				= 0x01,
    SCE_AVPLAYER_STATE_READY			= 0x02,
    SCE_AVPLAYER_STATE_PLAY				= 0x03,
    SCE_AVPLAYER_STATE_PAUSE			= 0x04,
    SCE_AVPLAYER_STATE_BUFFERING		= 0x05,
    SCE_AVPLAYER_TIMED_TEXT_DELIVERY	= 0x10,
    SCE_AVPLAYER_WARNING_ID				= 0x20,
    SCE_AVPLAYER_ENCRYPTION				= 0x30,
    SCE_AVPLAYER_DRM_ERROR				= 0x40
} SceAvPlayerEvents;

typedef struct SceAvPlayerAudio {
    uint16_t	channelCount;
    uint8_t		reserved1[2];
    uint32_t	sampleRate;
    uint32_t	size;
    uint8_t		languageCode[4];
} SceAvPlayerAudio;

typedef struct SceAvPlayerVideo {
    uint32_t	width;
    uint32_t	height;
    float		aspectRatio;
    uint8_t		languageCode[4];
} SceAvPlayerVideo;

typedef struct SceAvPlayerTextPosition{
    uint16_t	top;
    uint16_t	left;
    uint16_t	bottom;
    uint16_t	right;
} SceAvPlayerTextPosition;

typedef struct SceAvPlayerTimedText {
    uint8_t		languageCode[4];
    uint16_t	textSize;
    uint16_t	fontSize;
    SceAvPlayerTextPosition position;
} SceAvPlayerTimedText;

typedef union SceAvPlayerStreamDetails {
    uint8_t				reserved[16];
    SceAvPlayerAudio	audio;
    SceAvPlayerVideo	video;
    SceAvPlayerTimedText subs;
} SceAvPlayerStreamDetails;

typedef struct SceAvPlayerStreamInfo {
    uint32_t		type;
    uint8_t			reserved[4];
    SceAvPlayerStreamDetails details;
    uint64_t		duration;
    uint64_t		startTime;
} SceAvPlayerStreamInfo;

typedef struct SceAvPlayerFrameInfo {
    uint8_t*	pData;
    uint8_t		reserved[4];
    uint64_t	timeStamp;
    SceAvPlayerStreamDetails details;
} SceAvPlayerFrameInfo;

constexpr static std::array<char, 4> LANGUAGE_CODE_ENG={'E', 'N', 'G', '\0'};

struct PlayerState {
    constexpr static size_t BufferCount = 2;

    AVFormatContext* format_context;
    AVCodecContext* audio_codec_context;
    AVCodecContext* video_codec_context;
    std::queue<AVPacket*> audio_packets;
    std::queue<AVPacket*> video_packets;
    u64 last_video_timestamp;
    u64 last_audio_timestamp;
    std::array<u16, BufferCount> audio_buffer;
    std::array<u8, BufferCount> video_buffer;
    std::vector<u16> audio_chunk;
    std::vector<u16> video_chunk;
    std::chrono::milliseconds duration;
    u32 num_streams;
    s32 video_stream_id = -1;
    s32 audio_stream_id = -1;
    u32 num_channels;
    u32 num_samples;
    u32 sample_rate;
    std::string source;

    void CreateMedia(std::string_view source_path) {
        // Create ffmpeg format context.
        source = source_path;
        format_context = avformat_alloc_context();
        avformat_open_input(&format_context, source.c_str(), nullptr, nullptr);

        // Query stream duration.
        const auto duration_us = std::chrono::microseconds(format_context->duration);
        duration = std::chrono::duration_cast<std::chrono::milliseconds>(duration_us);
        LOG_INFO(Lib_AvPlayer, "format = {}, duration = {} ms", format_context->iformat->long_name, duration.count());

        // Deduce best stream type.
        num_streams = format_context->nb_streams;
        video_stream_id = av_find_best_stream(format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        audio_stream_id = av_find_best_stream(format_context, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

        // Initialize video codec.
        if (video_stream_id >= 0) {
            AVStream* video_stream = format_context->streams[video_stream_id];
            const AVCodec* video_codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
            video_codec_context = avcodec_alloc_context3(video_codec);
            avcodec_parameters_to_context(video_codec_context, video_stream->codecpar);
            avcodec_open2(video_codec_context, video_codec, nullptr);
            LOG_INFO(Lib_AvPlayer, "Video stream_id = {}, video codec = {}, resolution = {}x{}",
                     video_stream_id, video_codec->long_name, video_stream->codecpar->width,
                     video_stream->codecpar->height);
        }

        // Initialize audio codec.
        if (audio_stream_id >= 0) {
            AVStream* audio_stream = format_context->streams[audio_stream_id];
            const AVCodec* audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
            audio_codec_context = avcodec_alloc_context3(audio_codec);
            avcodec_parameters_to_context(audio_codec_context, audio_stream->codecpar);
            avcodec_open2(audio_codec_context, audio_codec, nullptr);
            LOG_INFO(Lib_AvPlayer, "Audio stream_id = {}, audio codec = {}, sample rate = {}",
                     audio_stream_id, audio_codec->long_name, audio_stream->codecpar->sample_rate);
        }
    }

    bool IsMediaAvailable() {
        return !source.empty();
    }

    bool NextPacket(s32 stream_id) {
        auto& this_queue = stream_id == video_stream_id ? video_packets : audio_packets;
        auto& other_queue = stream_id == video_stream_id ? audio_packets : video_packets;
        while (true) {
            if (!this_queue.empty()) {
                AVPacket* packet = this_queue.front();
                this_queue.pop();
                if (stream_id == video_stream_id) {
                    ASSERT(avcodec_send_packet(video_codec_context, packet) == 0);
                } else {
                    ASSERT(avcodec_send_packet(audio_codec_context, packet) == 0);
                }
                av_packet_free(&packet);
                return true;
            }
            AVPacket* packet = av_packet_alloc();
            if (av_read_frame(format_context, packet) != 0) {
                return false;
            }
            if (packet->stream_index == stream_id) {
                this_queue.push(packet);
            } else {
                other_queue.push(packet);
            }
        }
    }

    std::span<const u16> ReceiveAudio(bool ignore_is_playing = false) {
        if (audio_stream_id < 0) {
            return {};
        }
        if (source.empty()) {
            return {};
        }
        AVFrame* frame = av_frame_alloc();
        while (true) {
            int error = avcodec_receive_frame(audio_codec_context, frame);
            if (error == AVERROR(EAGAIN) && NextPacket(audio_stream_id)) {
                continue;
            }

            ASSERT(error == 0);
            if (frame->format != AV_SAMPLE_FMT_FLTP) {
                LOG_ERROR(Lib_AvPlayer, "Unknown audio format {}", frame->format);
            }
            last_audio_timestamp = av_rescale_q(frame->best_effort_timestamp,
                                                format_context->streams[audio_stream_id]->time_base,
                                                AV_TIME_BASE_Q);
            num_channels = frame->ch_layout.nb_channels;
            num_samples = frame->nb_samples;
            sample_rate = frame->sample_rate;
            audio_chunk.resize(num_samples * num_channels);
            for (u32 i = 0; i < num_samples; i++) {
                for (u32 j = 0; j < num_channels; j++) {
                    const auto* frame_data = reinterpret_cast<float*>(frame->data[j]);
                    const float current_sample = frame_data[i];
                    const s16 pcm_sample = current_sample * std::numeric_limits<s16>::max();
                    audio_chunk[i * frame->ch_layout.nb_channels + j] = pcm_sample;
                }
            }
            break;
        }

        av_frame_free(&frame);
        return audio_chunk;
    }
};

struct PlayerInfo {
    void* handle;
    u32 num_refs;
    std::mutex flock;
    PlayerState state;
    bool is_looped;
    bool is_paused;
    std::chrono::high_resolution_clock::time_point last_frame_time;
    SceAvPlayerMemAllocator memory_replacement;
    SceAvPlayerFileReplacement file_replacement;
    SceAvPlayerEventReplacement event_replacement;

    void EventCallback(s32 event_id, s32 source_id, void* event_data) {
        if (event_replacement.eventCallback) {
            event_replacement.eventCallback(event_replacement.objectPointer,
                                            event_id, source_id, event_data);
        }
    }

    s32 Open(const char* filename) {
        if (file_replacement.open) {
            return file_replacement.open(file_replacement.objectPointer, filename);
        }
        return 0;
    }

    s32 Close() {
        if (file_replacement.close) {
            return file_replacement.close(file_replacement.objectPointer);
        }
        return 0;
    }

    s64 Size() {
        if (file_replacement.size) {
            return file_replacement.size(file_replacement.objectPointer);
        }
        return 0;
    }

    void Deallocate(void* memory) {
        memory_replacement.deallocate(memory_replacement.objectPointer, memory);
    }

    void* Allocate(u32 alignment, u32 size) {
        return memory_replacement.allocate(memory_replacement.objectPointer,
                                           alignment, size);
    }

    void DeallocateTexture(void* memory) {
        memory_replacement.deallocateTexture(memory_replacement.objectPointer, memory);
    }

    void* AllocateTexture(u32 alignment, u32 size) {
        return memory_replacement.allocateTexture(memory_replacement.objectPointer,
                                                  alignment, size);
    }
};

typedef PlayerInfo* SceAvPlayerHandle;

s32 PS4_SYSV_ABI sceAvPlayerAddSource(SceAvPlayerHandle handle, const char* filename) {
    if (!handle || !filename) {
        return 0x806a0001;
    }

    // Open the video file and configure ffmpeg
    auto* mnt = Common::Singleton<Core::FileSys::MntPoints>::Instance();
    ASSERT(handle->Open(filename) >= 0);
    const auto host_path = mnt->GetHostFile(filename);
    handle->state.CreateMedia(host_path);

    // Notify guest about the change
    handle->EventCallback(SCE_AVPLAYER_STATE_BUFFERING, 0, nullptr);
    handle->EventCallback(SCE_AVPLAYER_STATE_READY, 0, nullptr);
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerAddSourceEx() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerChangeStream() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerClose(SceAvPlayerHandle player) {
    LOG_ERROR(Lib_AvPlayer, "called");
    player->EventCallback(SCE_AVPLAYER_STATE_STOP, 0, nullptr);
    delete player;
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerCurrentTime() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerDisableStream() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerEnableStream(SceAvPlayerHandle player, u32 stream_id) {
    LOG_INFO(Lib_AvPlayer, "called stream_id = {}", stream_id);
    if (!player) {
        return 0x806a0001;
    }
    if (stream_id > player->state.num_streams) {
        return 0x806a0002;
    }
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerGetAudioData() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerGetStreamInfo(SceAvPlayerHandle player, u32 stream_id,
                                          SceAvPlayerStreamInfo* argInfo) {
    LOG_INFO(Lib_AvPlayer, "called stream_id = {}", stream_id);
    if (!player || !argInfo) {
        return 0x806a0001;
    }

    auto& state = player->state;
    if (stream_id == state.video_stream_id) {
        argInfo->type = SCE_AVPLAYER_VIDEO;
        argInfo->duration = 5000;
        argInfo->startTime = 0;
        argInfo->details.video.width = state.video_codec_context->width;
        argInfo->details.video.height = state.video_codec_context->height;
        argInfo->details.video.aspectRatio = float(argInfo->details.video.width) / argInfo->details.video.height;
        std::memcpy(argInfo->details.video.languageCode, LANGUAGE_CODE_ENG.data(), 4);
        LOG_INFO(Lib_AvPlayer, "Video stream width = {}, height = {}, aspect ratio = {}",
                 argInfo->details.video.width, argInfo->details.video.height,
                 argInfo->details.video.aspectRatio);
    } else if (stream_id == state.audio_stream_id) {
        if (state.num_samples == 0) {
            state.ReceiveAudio(true);
        }
        argInfo->type = SCE_AVPLAYER_AUDIO;
        argInfo->duration = 5000;
        argInfo->startTime = 0;
        argInfo->details.audio.channelCount = state.num_channels;
        argInfo->details.audio.sampleRate = state.sample_rate;
        argInfo->details.audio.size = state.num_channels * state.num_samples * sizeof(u16);
        std::memcpy(argInfo->details.audio.languageCode, LANGUAGE_CODE_ENG.data(), 4);
        LOG_INFO(Lib_AvPlayer, "Audio stream num_channels = {}, sample_rate = {}, size = {}",
                 argInfo->details.audio.channelCount, argInfo->details.audio.sampleRate,
                 argInfo->details.audio.size);
    } else {
        argInfo->type = SCE_AVPLAYER_UNKNOWN;
    }
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerGetVideoData() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerGetVideoDataEx() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

SceAvPlayerHandle PS4_SYSV_ABI sceAvPlayerInit(SceAvPlayerInitData *pInit) {
    LOG_ERROR(Lib_AvPlayer, "called");

    // Allocate player from the provided memory callback.
    auto& mem_repl = pInit->memoryReplacement;
    auto* player = (PlayerInfo*)mem_repl.allocate(mem_repl.objectPointer, 0x20, sizeof(PlayerInfo));
    std::construct_at(player);

    // Initialize player.
    player->memory_replacement = pInit->memoryReplacement;
    player->file_replacement = pInit->fileReplacement;
    player->event_replacement = pInit->eventReplacement;
    player->is_paused = !pInit->autoStart;
    player->last_frame_time = std::chrono::high_resolution_clock::now();
    return player;
}

int PS4_SYSV_ABI sceAvPlayerInitEx() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerIsActive() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerJumpToTime() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerPause() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerPostInit() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerPrintf() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerResume() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerSetAvSyncMode() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerSetLogCallback() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerSetLooping() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerSetTrickSpeed() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerStart() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceAvPlayerStop() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerStreamCount(SceAvPlayerHandle handle) {
    if (!handle) {
        return 0x806a0001;
    }

    LOG_ERROR(Lib_AvPlayer, "called");
    return handle->state.num_streams;
}

int PS4_SYSV_ABI sceAvPlayerVprintf() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

void RegisterlibSceAvPlayer(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("KMcEa+rHsIo", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerAddSource);
    LIB_FUNCTION("x8uvuFOPZhU", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerAddSourceEx);
    LIB_FUNCTION("buMCiJftcfw", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerChangeStream);
    LIB_FUNCTION("NkJwDzKmIlw", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerClose);
    LIB_FUNCTION("wwM99gjFf1Y", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerCurrentTime);
    LIB_FUNCTION("BOVKAzRmuTQ", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerDisableStream);
    LIB_FUNCTION("ODJK2sn9w4A", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerEnableStream);
    LIB_FUNCTION("Wnp1OVcrZgk", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerGetAudioData);
    LIB_FUNCTION("d8FcbzfAdQw", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerGetStreamInfo);
    LIB_FUNCTION("o3+RWnHViSg", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerGetVideoData);
    LIB_FUNCTION("JdksQu8pNdQ", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerGetVideoDataEx);
    LIB_FUNCTION("aS66RI0gGgo", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerInit);
    LIB_FUNCTION("o9eWRkSL+M4", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerInitEx);
    LIB_FUNCTION("UbQoYawOsfY", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerIsActive);
    LIB_FUNCTION("XC9wM+xULz8", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerJumpToTime);
    LIB_FUNCTION("9y5v+fGN4Wk", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerPause);
    LIB_FUNCTION("HD1YKVU26-M", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerPostInit);
    LIB_FUNCTION("agig-iDRrTE", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerPrintf);
    LIB_FUNCTION("w5moABNwnRY", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerResume);
    LIB_FUNCTION("k-q+xOxdc3E", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerSetAvSyncMode);
    LIB_FUNCTION("eBTreZ84JFY", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerSetLogCallback);
    LIB_FUNCTION("OVths0xGfho", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerSetLooping);
    LIB_FUNCTION("av8Z++94rs0", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerSetTrickSpeed);
    LIB_FUNCTION("ET4Gr-Uu07s", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerStart);
    LIB_FUNCTION("ZC17w3vB5Lo", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerStop);
    LIB_FUNCTION("hdTyRzCXQeQ", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0,
                 sceAvPlayerStreamCount);
    LIB_FUNCTION("yN7Jhuv8g24", "libSceAvPlayer", 1, "libSceAvPlayer", 1, 0, sceAvPlayerVprintf);
};

} // namespace Libraries::AvPlayer
