// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <queue>
#include <mutex>
#include "common/assert.h"
#include "common/singleton.h"
#include "common/logging/log.h"
#include "common/io_file.h"
#include "common/path_util.h"
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

typedef struct SceAvPlayerAudioEx {
    uint16_t	channelCount;
    uint8_t		reserved[2];
    uint32_t	sampleRate;
    uint32_t	size;
    uint8_t		languageCode[4];
    uint8_t		reserved1[64];
} SceAvPlayerAudioEx;

typedef struct SceAvPlayerVideoEx {
    uint32_t	width;
    uint32_t	height;
    float		aspectRatio;
    uint8_t		languageCode[4];
    uint32_t	framerate;
    uint32_t	cropLeftOffset;
    uint32_t	cropRightOffset;
    uint32_t	cropTopOffset;
    uint32_t	cropBottomOffset;
    uint8_t		chromaBitDepth;
    bool		videoFullRangeFlag;
    uint8_t		reserved1[37];
} SceAvPlayerVideoEx;

typedef struct SceAvPlayerTimedTextEx {
    uint8_t		languageCode[4];
    uint8_t		reserved[12];
    uint8_t		reserved1[64];
} SceAvPlayerTimedTextEx;

typedef union SceAvPlayerStreamDetailsEx {
    SceAvPlayerAudioEx audio;
    SceAvPlayerVideoEx video;
    SceAvPlayerTimedTextEx subs;
    uint8_t		reserved1[80];
} SceAvPlayerStreamDetailsEx;

typedef struct SceAvPlayerFrameInfoEx {
    void* pData;
    uint8_t  reserved[4];
    uint64_t timeStamp;
    SceAvPlayerStreamDetailsEx details;
} SceAvPlayerFrameInfoEx;

constexpr static std::array<char, 4> LANGUAGE_CODE_ENG={'E', 'N', 'G', '\0'};

static std::chrono::microseconds CurrentTime() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch());
}

struct PlayerState {
    constexpr static size_t RingBufferCount = 2;

    SceAvPlayerMemAllocator* memory_replacement;
    AVFormatContext* format_context;
    AVCodecContext* audio_context;
    AVCodecContext* video_context;
    std::queue<AVPacket*> audio_packets;
    std::queue<AVPacket*> video_packets;
    u64 last_video_timestamp;
    u64 last_audio_timestamp;
    u32 audio_buffer_ring_index = 0;
    u32 audio_buffer_size = 0;
    std::array<u8*, RingBufferCount> audio_buffer;
    u32 video_buffer_ring_index = 0;
    u32 video_buffer_size = 0;
    std::array<u8*, RingBufferCount> video_buffer;
    std::vector<u16> audio_chunk;
    std::vector<u8> video_chunk;
    std::chrono::milliseconds duration;
    u32 num_streams;
    s32 video_stream_id = -1;
    s32 audio_stream_id = -1;
    u32 num_channels;
    u32 num_samples;
    u32 sample_rate;
    std::string video_playing;
    std::queue<std::string> videos_queue;

    void SwitchVideo(const std::string& source_path) {
        // Create ffmpeg format context.
        format_context = avformat_alloc_context();
        avformat_open_input(&format_context, source_path.c_str(), nullptr, nullptr);

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
            video_context = avcodec_alloc_context3(video_codec);
            avcodec_parameters_to_context(video_context, video_stream->codecpar);
            avcodec_open2(video_context, video_codec, nullptr);
            LOG_INFO(Lib_AvPlayer, "Video stream_id = {}, video codec = {}, resolution = {}x{}",
                     video_stream_id, video_codec->long_name, video_stream->codecpar->width,
                     video_stream->codecpar->height);
        }

        // Initialize audio codec.
        if (audio_stream_id >= 0) {
            AVStream* audio_stream = format_context->streams[audio_stream_id];
            const AVCodec* audio_codec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
            audio_context = avcodec_alloc_context3(audio_codec);
            avcodec_parameters_to_context(audio_context, audio_stream->codecpar);
            avcodec_open2(audio_context, audio_codec, nullptr);
            LOG_INFO(Lib_AvPlayer, "Audio stream_id = {}, audio codec = {}, sample rate = {}",
                     audio_stream_id, audio_codec->long_name, audio_stream->codecpar->sample_rate);
        }

        video_playing = source_path;
    }

    void FreeVideo() {
        if (video_context) {
            avcodec_free_context(&video_context);
        }
        if (audio_context) {
            avcodec_free_context(&audio_context);
        }
        if (format_context) {
            avformat_close_input(&format_context);
        }

        while (!video_packets.empty()) {
            AVPacket *packet = video_packets.front();
            av_packet_free(&packet);
            video_packets.pop();
        }
        while (!audio_packets.empty()) {
            AVPacket *packet = audio_packets.front();
            av_packet_free(&packet);
            audio_packets.pop();
        }
        video_playing.clear();
    }

    void Queue(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            LOG_INFO(Lib_AvPlayer, "Cannot find video: {}", path.string());
            return;
        }
        LOG_INFO(Lib_AvPlayer, "Queued video: {}", path.string());
        if (video_playing.empty()) {
            SwitchVideo(path);
        } else {
            videos_queue.push(path);
        }
    }

    bool IsMediaAvailable() {
        return !videos_queue.empty();
    }

    bool NextPacket(s32 stream_id) {
        auto& this_queue = stream_id == video_stream_id ? video_packets : audio_packets;
        auto& other_queue = stream_id == video_stream_id ? audio_packets : video_packets;
        while (true) {
            if (!this_queue.empty()) {
                AVPacket* packet = this_queue.front();
                this_queue.pop();
                if (stream_id == video_stream_id) {
                    ASSERT(avcodec_send_packet(video_context, packet) == 0);
                } else {
                    ASSERT(avcodec_send_packet(audio_context, packet) == 0);
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
        if (video_playing.empty()) {
            return {};
        }
        AVFrame* frame = av_frame_alloc();
        while (true) {
            const int error = avcodec_receive_frame(audio_context, frame);
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

    void CopyYuvDataFromFrame(AVFrame *frame, u8 *dest, const uint32_t width, const uint32_t height, bool is_p3) {
        for (uint32_t i = 0; i < height; i++) {
            memcpy(dest, &frame->data[0][frame->linesize[0] * i], width);
            dest += width;
        }

        if (is_p3) {
            for (uint32_t i = 0; i < height / 2; i++) {
                memcpy(dest, &frame->data[1][frame->linesize[1] * i], width / 2);
                dest += width / 2;
            }
            for (uint32_t i = 0; i < height / 2; i++) {
                memcpy(dest, &frame->data[2][frame->linesize[2] * i], width / 2);
                dest += width / 2;
            }
        } else {
            // p2 format, U and V are interleaved
            for (uint32_t i = 0; i < height / 2; i++) {
                const uint8_t *src_u = &frame->data[1][frame->linesize[1] * i];
                const uint8_t *src_v = &frame->data[2][frame->linesize[2] * i];
                for (uint32_t j = 0; j < width / 2; j++) {
                    dest[0] = src_u[j];
                    dest[1] = src_v[j];
                    dest += 2;
                }
            }
        }
    }

    std::span<const u8> ReceiveVideo() {
        if (video_stream_id < 0) {
            return {};
        }
        if (video_playing.empty()) {
            return {};
        }

        AVFrame *frame = av_frame_alloc();
        while (true) {
            const int error = avcodec_receive_frame(video_context, frame);
            if (error == AVERROR(EAGAIN) && NextPacket(video_stream_id)) {
                continue;
            }

            if (error != 0) {
                if (videos_queue.empty()) {
                    // Stop playing videos or
                    video_playing.clear();
                    break;
                } else {
                    // Play the next video (if there is any).
                    SwitchVideo(videos_queue.front());
                    videos_queue.pop();
                    continue;
                }
            }

            if (frame->format != AV_PIX_FMT_YUV420P) {
                LOG_ERROR(Lib_AvPlayer, "Unknown video format {}", frame->format);
            }

            last_video_timestamp = av_rescale_q(frame->best_effort_timestamp,
                                                format_context->streams[video_stream_id]->time_base,
                                                AV_TIME_BASE_Q);

            video_chunk.resize(video_context->width * video_context->height * 3 / 2);
            CopyYuvDataFromFrame(frame, video_chunk.data(), frame->width, frame->height, false);
            break;
        }

        av_frame_free(&frame);
        return video_chunk;
    }

    u8* GetBuffer(SceAvPlayerStreamType media_type,
                  u32 size, bool new_frame = true) {
        u32& buffer_size = media_type == SCE_AVPLAYER_VIDEO ? video_buffer_size : audio_buffer_size;
        u32& ring_index = media_type == SCE_AVPLAYER_VIDEO ? video_buffer_ring_index : audio_buffer_ring_index;
        auto& buffers = media_type == SCE_AVPLAYER_VIDEO ? video_buffer : audio_buffer;

        if (buffer_size < size) {
            buffer_size = size;
            for (u32 index = 0; index < RingBufferCount; index++) {
                const auto& mem = memory_replacement;
                if (buffers[index]) {
                    mem->deallocateTexture(mem->objectPointer, buffers[index]);
                }
                buffers[index] = (u8*)mem->allocateTexture(mem->objectPointer, 0x20, size);
            }
        }

        if (new_frame) {
            ring_index++;
        }
        return buffers[ring_index % RingBufferCount];
    }

    u32 GetH264BufferSize() const {
        return video_context->width * video_context->height * 3 / 2;
    }

    std::chrono::microseconds GetFramerate() const {
        const AVRational rational = format_context->streams[video_stream_id]->avg_frame_rate;
        return std::chrono::seconds(rational.den / rational.num);
    }
};

struct PlayerInfo {
    void* handle;
    u32 num_refs;
    PlayerState state;
    bool is_looped;
    bool is_paused = true;
    std::chrono::microseconds last_frame_time;
    SceAvPlayerMemAllocator memory_replacement;
    SceAvPlayerFileReplacement file_replacement;
    SceAvPlayerEventReplacement event_replacement;

    PlayerInfo() {
        state.memory_replacement = &memory_replacement;
    }

    bool HasFileReplacements() const noexcept {
        return file_replacement.open && file_replacement.readOffset &&
               file_replacement.close;
    }

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

    s32 ReadOffset(u8* buffer, u64 pos, u32 buf_len) {
        if (file_replacement.readOffset) {
            return file_replacement.readOffset(file_replacement.objectPointer, buffer, pos, buf_len);
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

static std::recursive_mutex mutex;

s32 PS4_SYSV_ABI sceAvPlayerAddSource(SceAvPlayerHandle player, const char* filename) {
    std::scoped_lock lk{mutex};
    if (!player || !filename) {
        return 0x806a0001;
    }

    // Open the video file and configure ffmpeg
    auto* mnt = Common::Singleton<Core::FileSys::MntPoints>::Instance();
    std::filesystem::path filepath = mnt->GetHostFile(filename);
    if (!std::filesystem::exists(filepath) && player->HasFileReplacements()) {
        // Games can pass custom paths to avplayer that we can't open directly.
        ASSERT(player->Open(filename) >= 0);
        const u32 size = player->Size();

        // Read the file and dump it to the directory so ffmpeg can read it.
        const auto dump_path = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
        const auto file = std::string_view(filename);
        const auto dump_name = file.substr(file.find_last_of('/') + 1);
        filepath = dump_path / dump_name;
        static constexpr u32 BufferSize = 512_KB;
        Common::FS::IOFile temp_file{filepath, Common::FS::FileAccessMode::Write};
        std::vector<u8> buffer(BufferSize);
        u32 bytes_remaining = size;
        u32 offset = 0;
        while (bytes_remaining > 0) {
            const u32 buf_size = std::min(BufferSize, bytes_remaining);
            player->ReadOffset(buffer.data(), offset, buf_size);
            temp_file.WriteRaw<u8>(buffer.data(), buf_size);
            offset += buf_size;
            bytes_remaining -= buf_size;
        }
        temp_file.Close();
        player->Close();
    }
    player->state.Queue(filepath);

    // Notify guest about the change
    player->EventCallback(SCE_AVPLAYER_STATE_BUFFERING, 0, nullptr);
    player->EventCallback(SCE_AVPLAYER_STATE_READY, 0, nullptr);
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
    std::scoped_lock lk{mutex};
    LOG_ERROR(Lib_AvPlayer, "called");
    player->EventCallback(SCE_AVPLAYER_STATE_STOP, 0, nullptr);
    player->Deallocate(player);
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
    std::scoped_lock lk{mutex};
    LOG_INFO(Lib_AvPlayer, "called stream_id = {}", stream_id);
    if (!player) {
        return 0x806a0001;
    }
    if (stream_id > player->state.num_streams) {
        return 0x806a0002;
    }
    return ORBIS_OK;
}

bool PS4_SYSV_ABI sceAvPlayerGetAudioData(SceAvPlayerHandle player,
                                          SceAvPlayerFrameInfo* audioInfo) {
    std::scoped_lock lk{mutex};
    LOG_INFO(Lib_AvPlayer, "called");
    if (!player || !audioInfo) {
        return false;
    }
    auto& state = player->state;
    if (player->is_paused) {
        return false;
    }

    const auto audio_data = state.ReceiveAudio(true);
    if (audio_data.empty()) {
        return false;
    }

    u8* buffer = state.GetBuffer(SCE_AVPLAYER_AUDIO, audio_data.size_bytes(), false);
    std::memcpy(buffer, audio_data.data(), audio_data.size_bytes());
    audioInfo->pData = buffer;
    audioInfo->timeStamp = state.last_audio_timestamp / 1000;
    audioInfo->details.audio.channelCount = state.num_channels;
    audioInfo->details.audio.sampleRate = state.sample_rate;
    audioInfo->details.audio.size = state.num_channels * state.num_samples * sizeof(u16);
    return true;
}

s32 PS4_SYSV_ABI sceAvPlayerGetStreamInfo(SceAvPlayerHandle player, u32 stream_id,
                                          SceAvPlayerStreamInfo* argInfo) {
    std::scoped_lock lk{mutex};
    LOG_INFO(Lib_AvPlayer, "called stream_id = {}", stream_id);
    if (!player || !argInfo) {
        return 0x806a0001;
    }

    auto& state = player->state;
    if (stream_id == state.video_stream_id) {
        argInfo->type = SCE_AVPLAYER_VIDEO;
        argInfo->duration = 5000;
        argInfo->startTime = 0;
        argInfo->details.video.width = state.video_context->width;
        argInfo->details.video.height = state.video_context->height;
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

bool PS4_SYSV_ABI sceAvPlayerGetVideoDataEx(SceAvPlayerHandle player,
                                            SceAvPlayerFrameInfoEx *videoInfo) {
    std::scoped_lock lk{mutex};
    LOG_TRACE(Lib_AvPlayer, "called");
    if (!player || !videoInfo || player->is_paused) {
        return false;
    }

    auto& state = player->state;
    const auto framerate = state.GetFramerate();
    if (player->last_frame_time + framerate < CurrentTime()) {
        player->last_frame_time += framerate;
        u8* buffer = state.GetBuffer(SCE_AVPLAYER_VIDEO, state.GetH264BufferSize(), true);
        const auto video_data = state.ReceiveVideo();
        const size_t video_size = video_data.size_bytes();
        std::memcpy(buffer, video_data.data(), video_data.size_bytes());
        videoInfo->pData = buffer;
    } else {
        u8* buffer = state.GetBuffer(SCE_AVPLAYER_VIDEO, state.GetH264BufferSize(), false);
        videoInfo->pData = buffer;
    }

    videoInfo->timeStamp = state.last_video_timestamp / 1000;
    videoInfo->details.video.width = state.video_context->width;
    videoInfo->details.video.height = state.video_context->height;
    videoInfo->details.video.aspectRatio = float(state.video_context->width) / state.video_context->height;
    videoInfo->details.video.framerate = 0;
    std::memcpy(videoInfo->details.video.languageCode, LANGUAGE_CODE_ENG.data(), 4);
    videoInfo->details.video.chromaBitDepth = 8;
    return true;
}

SceAvPlayerHandle PS4_SYSV_ABI sceAvPlayerInit(SceAvPlayerInitData *pInit) {
    std::scoped_lock lk{mutex};
    LOG_ERROR(Lib_AvPlayer, "called");

    // Allocate player from the provided memory callback.
    auto& mem_repl = pInit->memoryReplacement;
    auto* player = (PlayerInfo*)mem_repl.allocate(mem_repl.objectPointer, 0x20, sizeof(PlayerInfo));
    std::construct_at(player);

    // Initialize player.
    player->memory_replacement = pInit->memoryReplacement;
    player->file_replacement = pInit->fileReplacement;
    player->event_replacement = pInit->eventReplacement;
    player->last_frame_time = CurrentTime();
    return player;
}

int PS4_SYSV_ABI sceAvPlayerInitEx() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

bool PS4_SYSV_ABI sceAvPlayerIsActive(SceAvPlayerHandle player) {
    std::scoped_lock lk{mutex};
    LOG_TRACE(Lib_AvPlayer, "called");
    if (!player) {
        return false;
    }
    return !player->state.video_playing.empty();
}

int PS4_SYSV_ABI sceAvPlayerJumpToTime() {
    LOG_ERROR(Lib_AvPlayer, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerPause(SceAvPlayerHandle player) {
    std::scoped_lock lk{mutex};
    LOG_INFO(Lib_AvPlayer, "called");
    player->is_paused = true;
    player->EventCallback(SCE_AVPLAYER_STATE_PAUSE, 0, nullptr);
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

s32 PS4_SYSV_ABI sceAvPlayerResume(SceAvPlayerHandle player) {
    std::scoped_lock lk{mutex};
    LOG_INFO(Lib_AvPlayer, "called");
    if (player->is_paused) {
        player->EventCallback(SCE_AVPLAYER_STATE_PLAY, 0, nullptr);
    }
    player->is_paused = false;
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

s32 PS4_SYSV_ABI sceAvPlayerStart(SceAvPlayerHandle player) {
    std::scoped_lock lk{mutex};
    LOG_INFO(Lib_AvPlayer, "called");
    player->is_paused = false;
    player->EventCallback(SCE_AVPLAYER_STATE_PLAY, 0, nullptr);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerStop(SceAvPlayerHandle player) {
    std::scoped_lock lk{mutex};
    LOG_INFO(Lib_AvPlayer, "called");
    player->state.FreeVideo();
    player->is_paused = true;
    player->EventCallback(SCE_AVPLAYER_STATE_STOP, 0, nullptr);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceAvPlayerStreamCount(SceAvPlayerHandle handle) {
    std::scoped_lock lk{mutex};
    if (!handle) {
        return 0x806a0001;
    }

    LOG_ERROR(Lib_AvPlayer, "called");
    return /*handle->state.num_streams*/2;
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
