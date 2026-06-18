// RT64 renderer context for Gex 3 Recompiled.
// Adapted from Zelda64Recomp's rt64_render_context.cpp (MIT license),
// with texture-pack/mod/UI machinery removed.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

#include "hle/rt64_application.h"

#include "ultramodern/renderer_context.hpp"
#include "ultramodern/ultramodern.hpp"

static uint8_t DMEM[0x1000];
static uint8_t IMEM[0x1000];

static unsigned int MI_INTR_REG = 0;
static unsigned int DPC_START_REG = 0;
static unsigned int DPC_END_REG = 0;
static unsigned int DPC_CURRENT_REG = 0;
static unsigned int DPC_STATUS_REG = 0;
static unsigned int DPC_CLOCK_REG = 0;
static unsigned int DPC_BUFBUSY_REG = 0;
static unsigned int DPC_PIPEBUSY_REG = 0;
static unsigned int DPC_TMEM_REG = 0;

static void dummy_check_interrupts() {}

namespace gex {

class RT64Context final : public ultramodern::renderer::RendererContext {
public:
    RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool debug) {
        static unsigned char dummy_rom_header[0x40];

        RT64::Application::Core appCore{};
        appCore.window = window_handle;  // SDL_Window* on Linux
        appCore.checkInterrupts = dummy_check_interrupts;

        appCore.HEADER = dummy_rom_header;
        appCore.RDRAM = rdram;
        appCore.DMEM = DMEM;
        appCore.IMEM = IMEM;

        appCore.MI_INTR_REG = &MI_INTR_REG;
        appCore.DPC_START_REG = &DPC_START_REG;
        appCore.DPC_END_REG = &DPC_END_REG;
        appCore.DPC_CURRENT_REG = &DPC_CURRENT_REG;
        appCore.DPC_STATUS_REG = &DPC_STATUS_REG;
        appCore.DPC_CLOCK_REG = &DPC_CLOCK_REG;
        appCore.DPC_BUFBUSY_REG = &DPC_BUFBUSY_REG;
        appCore.DPC_PIPEBUSY_REG = &DPC_PIPEBUSY_REG;
        appCore.DPC_TMEM_REG = &DPC_TMEM_REG;

        ultramodern::renderer::ViRegs* vi_regs = ultramodern::renderer::get_vi_regs();
        appCore.VI_STATUS_REG = &vi_regs->VI_STATUS_REG;
        appCore.VI_ORIGIN_REG = &vi_regs->VI_ORIGIN_REG;
        appCore.VI_WIDTH_REG = &vi_regs->VI_WIDTH_REG;
        appCore.VI_INTR_REG = &vi_regs->VI_INTR_REG;
        appCore.VI_V_CURRENT_LINE_REG = &vi_regs->VI_V_CURRENT_LINE_REG;
        appCore.VI_TIMING_REG = &vi_regs->VI_TIMING_REG;
        appCore.VI_V_SYNC_REG = &vi_regs->VI_V_SYNC_REG;
        appCore.VI_H_SYNC_REG = &vi_regs->VI_H_SYNC_REG;
        appCore.VI_LEAP_REG = &vi_regs->VI_LEAP_REG;
        appCore.VI_H_START_REG = &vi_regs->VI_H_START_REG;
        appCore.VI_V_START_REG = &vi_regs->VI_V_START_REG;
        appCore.VI_V_BURST_REG = &vi_regs->VI_V_BURST_REG;
        appCore.VI_X_SCALE_REG = &vi_regs->VI_X_SCALE_REG;
        appCore.VI_Y_SCALE_REG = &vi_regs->VI_Y_SCALE_REG;

        RT64::ApplicationConfiguration appConfig;
        appConfig.useConfigurationFile = false;

        app = std::make_unique<RT64::Application>(appCore, appConfig);

        app->userConfig.resolution = RT64::UserConfiguration::Resolution::WindowIntegerScale;
        app->userConfig.downsampleMultiplier = 1;
        app->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Expand;
        app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Manual;
        app->userConfig.refreshRateTarget = 60;
        app->userConfig.internalColorFormat = RT64::UserConfiguration::InternalColorFormat::Automatic;
        app->userConfig.displayBuffering = RT64::UserConfiguration::DisplayBuffering::Triple;
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Automatic;
        app->userConfig.developerMode = true;
        // Force gbi depth branches to prevent LODs from kicking in.
        app->enhancementConfig.f3dex.forceBranch = true;
        app->enhancementConfig.textureLOD.scale = true;

        switch (app->setup(0)) {
            case RT64::Application::SetupResult::Success:
                setup_result = ultramodern::renderer::SetupResult::Success;
                break;
            case RT64::Application::SetupResult::DynamicLibrariesNotFound:
                setup_result = ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
                break;
            case RT64::Application::SetupResult::InvalidGraphicsAPI:
                setup_result = ultramodern::renderer::SetupResult::InvalidGraphicsAPI;
                break;
            case RT64::Application::SetupResult::GraphicsAPINotFound:
                setup_result = ultramodern::renderer::SetupResult::GraphicsAPINotFound;
                break;
            case RT64::Application::SetupResult::GraphicsDeviceNotFound:
                setup_result = ultramodern::renderer::SetupResult::GraphicsDeviceNotFound;
                break;
        }
        chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;
        if (setup_result != ultramodern::renderer::SetupResult::Success) {
            fprintf(stderr, "[rt64] setup failed (%d)\n", (int)setup_result);
            app = nullptr;
            return;
        }
        fprintf(stderr, "[rt64] renderer initialized\n");
    }

    ~RT64Context() override = default;

    bool valid() override { return static_cast<bool>(app); }

    bool update_config(const ultramodern::renderer::GraphicsConfig&,
                       const ultramodern::renderer::GraphicsConfig&) override {
        return false;
    }

    void enable_instant_present() override {
        app->enhancementConfig.presentation.mode =
            RT64::EnhancementConfiguration::Presentation::Mode::PresentEarly;
        app->updateEnhancementConfig();
    }

    void send_dl(const OSTask* task) override {
        static int n = 0;
        ++n;
        // fprintf(stderr, "[gfx] dl #%d ucode=%08X data=%08X\n", n,
        //         (uint32_t)task->t.ucode, (uint32_t)task->t.data_ptr);
        app->state->rsp->reset();
        app->interpreter->loadUCodeGBI(task->t.ucode & 0x3FFFFFF, task->t.ucode_data & 0x3FFFFFF, true);
        app->processDisplayLists(app->core.RDRAM, task->t.data_ptr & 0x3FFFFFF, 0, true);
        // fprintf(stderr, "[gfx] dl #%d done\n", n);
    }

    void update_screen() override {
        static const bool trace = getenv("GEX3_GFX_TRACE") != nullptr;
        if (trace) {
            static int n = 0;
            if (n++ % 60 == 0) {
                ultramodern::renderer::ViRegs* v = ultramodern::renderer::get_vi_regs();
                fprintf(stderr, "[gfx] update_screen #%d VI_ORIGIN=%08X WIDTH=%X\n",
                        n, v->VI_ORIGIN_REG, v->VI_WIDTH_REG);
            }
        }
        app->updateScreen();
    }

    void shutdown() override {
        if (app != nullptr) {
            app->end();
        }
    }

    uint32_t get_display_framerate() const override {
        return app->presentQueue->ext.sharedResources->swapChainRate;
    }

    float get_resolution_scale() const override {
        constexpr int ReferenceHeight = 240;
        if (app->sharedQueueResources->swapChainHeight > 0) {
            return std::max(float((app->sharedQueueResources->swapChainHeight + ReferenceHeight - 1) / ReferenceHeight), 1.0f);
        }
        return 1.0f;
    }

private:
    std::unique_ptr<RT64::Application> app;
};

std::unique_ptr<ultramodern::renderer::RendererContext>
create_rt64_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode) {
    return std::make_unique<RT64Context>(rdram, window_handle, developer_mode);
}

}  // namespace gex
