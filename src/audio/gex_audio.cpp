#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>

#include <SDL2/SDL.h>

#include <librecomp/game.hpp>
#include <librecomp/rsp.hpp>
#include "recomp.h"
#include "gex_audio.h"

extern RspUcodeFunc aspMain;

// ---------- Audio state and diagnostics ----------
static const OSTask* asp_task = nullptr;
static bool g_asp_cmd_active = false;
static uint8_t* g_rdram = nullptr;
static int g_envmix_idx = 0;
static int g_asp_frame = -1;

static std::mutex audio_mutex;
static SDL_AudioDeviceID audio_device = 0;
static double audio_buffered_frames = 0.0;
static uint32_t audio_freq_hz = 48000;
static std::chrono::steady_clock::time_point audio_last = std::chrono::steady_clock::now();

static int16_t dmem_s16(uint32_t o) {
    uint8_t hi = dmem[(o ^ 3) & 0xFFF], lo = dmem[((o + 1) ^ 3) & 0xFFF];
    return (int16_t)((hi << 8) | lo);
}

extern "C" void gex_voice_reset(void) { g_envmix_idx = 0; g_asp_frame++; }

extern "C" int gex_voice_skip(void) {
    static int init = 0, solo = -1, mute = -1, firstk = -1;
    if (!init) {
        init = 1;
        if (const char* s = getenv("GEX3_SOLO"))   solo   = atoi(s);
        if (const char* s = getenv("GEX3_MUTE"))   mute   = atoi(s);
        if (const char* s = getenv("GEX3_FIRSTK")) firstk = atoi(s);
    }
    int idx = g_envmix_idx++;
    if (solo   >= 0 && idx != solo)   return 1;
    if (mute   >= 0 && idx == mute)   return 1;
    if (firstk >= 0 && idx >= firstk) return 1;
    return 0;
}

extern "C" void gex_fill_window(uint8_t* rdram, uint32_t desc, uint32_t addr) {
    (void)addr;
    if (desc == 0) return;
    gpr d = (gpr)(int32_t)desc;
    uint32_t start = MEM_W(0xC, d), ptr = MEM_W(0x10, d);
    uint32_t winLen = MEM_W(0x0, (gpr)(int32_t)0x8008F964u);
    if (winLen == 0 || winLen > 0x4000) winLen = 0x800;
    auto rom = recomp::get_rom();
    if ((size_t)start + winLen > rom.size()) winLen = rom.size() > start ? (uint32_t)(rom.size() - start) : 0;
    if (winLen) {
        if (((start & 7u) == 0) && ((ptr & 7u) == 0)) {
            recomp::do_rom_read(rdram, (gpr)(int32_t)ptr, 0x10000000u + start, winLen);
        } else {
            const uint8_t* rd = rom.data();
            gpr p = (gpr)(int32_t)ptr;
            for (uint32_t i = 0; i < winLen; i++) MEM_B(i, p) = (int8_t)rd[start + i];
        }
    }
}

extern "C" void asp_cmd_snapshot(uint8_t* rdram, uint32_t w0, uint32_t w1) {
    if (getenv("GEX3_RESAMP_FIX")) {
        static uint8_t save[0x40]; static bool have = false;
        uint32_t op = w0 >> 24;
        uint32_t lo = 0x4C0u, hi = 0x4E0u;
        if (op == 0x07) { for (uint32_t o = lo; o < hi; o++) save[o - lo] = dmem[(o ^ 3) & 0xFFF]; have = true; }
        else if (op == 0x05 && have) { for (uint32_t o = lo; o < hi; o++) dmem[(o ^ 3) & 0xFFF] = save[o - lo]; }
    }
}

extern "C" void asp_dump_decode(uint8_t* rdram, uint32_t state_dmem, uint32_t state_ram) {}

extern "C" RspExitReason asp_logged(uint8_t* rdram, uint32_t ucode_addr) {
    static int n = 0;
    g_asp_cmd_active = (getenv("GEX3_ASP_CMD") != nullptr) && (n == 10);
    gex_voice_reset();
    RspExitReason r = aspMain(rdram, ucode_addr);
    n++;
    return r;
}

namespace gex {

static void audio_advance_clock() {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - audio_last).count();
    audio_last = now;
    audio_buffered_frames = std::max(0.0, audio_buffered_frames - dt * audio_freq_hz);
}

static float audio_gain() {
    static float g = -1.0f;
    if (g < 0.0f) { const char* e = getenv("GEX3_AUDIO_GAIN"); g = e ? (float)atof(e) : 1.0f; if (g < 0) g = 0; }
    return g;
}

void audio_queue_samples(int16_t* samples, size_t count) {
    std::lock_guard lock(audio_mutex);
    float g = audio_gain();
    if (g != 1.0f) {
        for (size_t i = 0; i < count; i++) {
            int32_t s = (int32_t)lrintf(samples[i] * g);
            samples[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
    }
    if (audio_device != 0) {
        static std::vector<int16_t> swapped;
        swapped.resize(count);
        for (size_t i = 0; i < count; i += 2) {
            swapped[i] = samples[i + 1];
            swapped[i + 1] = samples[i];
        }
        SDL_QueueAudio(audio_device, swapped.data(), count * sizeof(int16_t));
    } else {
        audio_advance_clock();
        audio_buffered_frames += count / 2.0;
    }
}

size_t audio_frames_remaining() {
    std::lock_guard lock(audio_mutex);
    if (audio_device != 0) {
        size_t queued = SDL_GetQueuedAudioSize(audio_device) / (2 * sizeof(int16_t));
        const char* e = getenv("GEX3_AUDIO_CUSHION_MS");
        int ms = e ? atoi(e) : 40;
        size_t cushion = (size_t)((uint64_t)audio_freq_hz * (uint32_t)std::max(0, ms) / 1000);
        return queued > cushion ? queued - cushion : 0;
    }
    audio_advance_clock();
    return (size_t)audio_buffered_frames;
}

void audio_set_frequency(uint32_t freq) {
    std::lock_guard lock(audio_mutex);
    audio_freq_hz = freq ? freq : 48000;
    if (getenv("GEX3_NULL_RENDERER")) return;
    if (audio_device != 0) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = (int)audio_freq_hz;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (audio_device != 0) {
        SDL_PauseAudioDevice(audio_device, 0);
    }
}

} // namespace gex
