// Gex 3: Deep Cover Gecko — native PC host application.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>

#include "xxhash.h"

#include <SDL2/SDL.h>

#include <librecomp/game.hpp>
#include <librecomp/overlays.hpp>
#include <librecomp/rsp.hpp>
#include <ultramodern/ultramodern.hpp>
#include <ultramodern/renderer_context.hpp>
#include <ultramodern/error_handling.hpp>

#include "recomp.h"
#include "recomp_overlays.inl"
#include "gex_audio.h"
#include "gex_input.h"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);

namespace gex {
std::unique_ptr<ultramodern::renderer::RendererContext>
create_rt64_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);
}

// ---------- null renderer ----------
class NullRenderer final : public ultramodern::renderer::RendererContext {
public:
    NullRenderer() { setup_result = ultramodern::renderer::SetupResult::Success; }
    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&,
                       const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    void send_dl(const OSTask*) override {
        static int n = 0;
        if (n++ % 100 == 0) fprintf(stderr, "[gfx] display list task #%d\n", n);
    }
    void update_screen() override {}
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1.0f; }
};

static std::unique_ptr<ultramodern::renderer::RendererContext>
create_render_context(uint8_t*, ultramodern::renderer::WindowHandle, bool) {
    return std::make_unique<NullRenderer>();
}

// ---------- RSP microcode handling ----------
static RspExitReason dummy_ucode(uint8_t*, uint32_t) {
    static int n = 0;
    if (n++ % 100 == 0) fprintf(stderr, "[rsp] task #%d\n", n);
    return RspExitReason::Broke;
}

static RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    if (task->t.type == 2 /* M_AUDTASK */) {
        return asp_logged;
    }
    fprintf(stderr, "[rsp] non-audio task type=%u ucode=%08X\n",
            (uint32_t)task->t.type, (uint32_t)task->t.ucode);
    return dummy_ucode;
}

// ---------- SDL window / events ----------
static bool use_null_renderer() { return getenv("GEX3_NULL_RENDERER") != nullptr; }

static void* gfx_create() {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER);
    return nullptr;
}

static ultramodern::renderer::WindowHandle gfx_create_window(void*) {
    SDL_Window* window = SDL_CreateWindow(
        "Gex 3: Deep Cover Gecko — Recompiled", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, 1280, 960, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (window == nullptr) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        std::exit(1);
    }
    return window;
}

SDL_GameController* g_gamepad = nullptr;

static void gfx_update(void*) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
            case SDL_QUIT: {
                ultramodern::quit();
                static std::thread watchdog([] {
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    fprintf(stderr, "[quit] forcing exit\n");
                    std::_Exit(0);
                });
                watchdog.detach();
                break;
            }
            case SDL_CONTROLLERDEVICEADDED:
                if (g_gamepad == nullptr) {
                    g_gamepad = SDL_GameControllerOpen(ev.cdevice.which);
                    fprintf(stderr, "[input] gamepad connected: %s\n", SDL_GameControllerName(g_gamepad));
                }
                break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (g_gamepad != nullptr && ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_gamepad))) {
                    SDL_GameControllerClose(g_gamepad);
                    g_gamepad = nullptr;
                }
                break;
        }
    }
}

static void on_vi() {}
static void on_gfx_init() { fprintf(stderr, "[gfx] init\n"); }
static void msg_box(const char* m) { fprintf(stderr, "[msgbox] %s\n", m); }

int main(int argc, char** argv) {
    std::filesystem::path rom_path = argc > 1 ? argv[1] : "../gex3.z64";
    std::filesystem::create_directories("./gex3_data");
    recomp::register_config_path("./gex3_data");
    std::ifstream rf(rom_path, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    if (rom.empty()) { fprintf(stderr, "cannot read rom %s\n", rom_path.c_str()); return 1; }
    uint64_t hash = XXH3_64bits(rom.data(), rom.size());
    fprintf(stderr, "[boot] rom %zu bytes, xxh3 %016lx\n", rom.size(), hash);
    recomp::GameEntry game{};
    game.rom_hash = hash;
    game.game_id = u8"gex3";
    game.mod_game_id = "gex3";
    game.entrypoint_address = (gpr)(int32_t)0x80000400u;
    game.entrypoint = recomp_entrypoint;
    recomp::register_game(game);
    recomp::overlays::overlay_section_table_data_t sections{section_table, num_sections, num_sections};
    recomp::overlays::overlays_by_index_t overlays{overlay_sections_by_index, sizeof(overlay_sections_by_index) / sizeof(overlay_sections_by_index[0])};
    recomp::overlays::register_overlays(sections, overlays);
    std::u8string game_id = u8"gex3";
    recomp::select_rom(rom_path, game_id);
    recomp::start_game(game_id);
    recomp::Configuration cfg{};
    cfg.rsp_callbacks = {.get_rsp_microcode = get_rsp_microcode};
    cfg.audio_callbacks = {.queue_samples = gex::audio_queue_samples,
                           .get_frames_remaining = gex::audio_frames_remaining,
                           .set_frequency = gex::audio_set_frequency};
    cfg.input_callbacks = {.poll_input = gex::input_poll, .get_input = gex::input_get, .set_rumble = gex::input_rumble, .get_connected_device_info = gex::input_device_info};
    cfg.events_callbacks = {.vi_callback = on_vi, .gfx_init_callback = on_gfx_init};
    cfg.error_handling_callbacks = {.message_box = msg_box};
    if (use_null_renderer()) {
        cfg.window_handle = reinterpret_cast<SDL_Window*>(0x1);
        cfg.renderer_callbacks = {.create_render_context = create_render_context};
    } else {
        cfg.renderer_callbacks = {.create_render_context = gex::create_rt64_context};
        cfg.gfx_callbacks = {.create_gfx = gfx_create, .create_window = gfx_create_window, .update_gfx = gfx_update};
    }
    recomp::start(cfg);
    return 0;
}
