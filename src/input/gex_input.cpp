#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <SDL2/SDL.h>
#include <ultramodern/ultramodern.hpp>
#include "recomp.h"
#include "gex_input.h"

extern SDL_GameController* g_gamepad; 

namespace gex {

void input_poll() {
    // SDL events are handled in gfx_update for now to keep SDL event loop unified
}

bool input_get(int n, uint16_t* buttons, float* x, float* y) {
    *buttons = 0; *x = 0.0f; *y = 0.0f;
    if (n != 0) return false;
    if (getenv("GEX3_NULL_RENDERER")) return true;

    const Uint8* k = SDL_GetKeyboardState(nullptr);
    uint16_t b = 0;
    if (k[SDL_SCANCODE_X])      b |= 0x8000;  // A
    if (k[SDL_SCANCODE_C])      b |= 0x4000;  // B
    if (k[SDL_SCANCODE_LSHIFT]) b |= 0x2000;  // Z
    if (k[SDL_SCANCODE_RETURN]) b |= 0x1000;  // Start
    if (k[SDL_SCANCODE_W])      b |= 0x0008;  // C-up
    if (k[SDL_SCANCODE_S])      b |= 0x0004;  // C-down
    if (k[SDL_SCANCODE_A])      b |= 0x0002;  // C-left
    if (k[SDL_SCANCODE_D])      b |= 0x0001;  // C-right
    if (k[SDL_SCANCODE_Q])      b |= 0x0020;  // L
    if (k[SDL_SCANCODE_E])      b |= 0x0010;  // R
    
    float xs = 0.0f, ys = 0.0f;
    if (k[SDL_SCANCODE_LEFT])  xs -= 1.0f;
    if (k[SDL_SCANCODE_RIGHT]) xs += 1.0f;
    if (k[SDL_SCANCODE_DOWN])  ys -= 1.0f;
    if (k[SDL_SCANCODE_UP])    ys += 1.0f;

    if (g_gamepad != nullptr) {
        auto btn = [&](SDL_GameControllerButton sb) { return SDL_GameControllerGetButton(g_gamepad, sb) != 0; };
        if (btn(SDL_CONTROLLER_BUTTON_A)) b |= 0x8000;
        if (btn(SDL_CONTROLLER_BUTTON_X)) b |= 0x4000;
        if (btn(SDL_CONTROLLER_BUTTON_START)) b |= 0x1000;
        if (btn(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= 0x0020;
        if (btn(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= 0x0010;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= 0x0800;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= 0x0400;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= 0x0200;
        if (btn(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= 0x0100;
        if (SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 8000 || btn(SDL_CONTROLLER_BUTTON_B)) b |= 0x2000;
        
        Sint16 rx = SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_RIGHTX);
        Sint16 ry = SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_RIGHTY);
        if (ry < -12000) b |= 0x0008; if (ry > 12000) b |= 0x0004; if (rx < -12000) b |= 0x0002; if (rx > 12000) b |= 0x0001;
        
        Sint16 lx = SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_LEFTX);
        Sint16 ly = SDL_GameControllerGetAxis(g_gamepad, SDL_CONTROLLER_AXIS_LEFTY);
        if (lx > 8000 || lx < -8000) xs = lx / 32767.0f;
        if (ly > 8000 || ly < -8000) ys = -ly / 32767.0f;
    }

    if (getenv("GEX3_AUTOPRESS")) {
        uint32_t t = SDL_GetTicks();
        if ((t / 500) % 2 == 0) b |= ((t / 1000) % 2) ? 0x1000 : 0x8000;
    }

    *buttons = b; *x = xs; *y = ys;
    return true;
}

void input_rumble(int n, bool rumble) {}

ultramodern::input::connected_device_info_t input_device_info(int n) {
    if (getenv("GEX3_NO_CONTROLLER")) return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
    if (n == 0) return {ultramodern::input::Device::Controller, ultramodern::input::Pak::ControllerPak};
    return {ultramodern::input::Device::None, ultramodern::input::Pak::None};
}

} // namespace gex

// ---------- Controller Pak (Memory Pak) emulation ----------
static uint8_t g_mpk[0x8000];
static bool g_mpk_loaded = false;
static const char* MPK_PATH = "gex3_data/gex3.mpk";

static void mpk_load() {
    if (g_mpk_loaded) return;
    g_mpk_loaded = true;
    memset(g_mpk, 0, sizeof(g_mpk));
    if (FILE* f = fopen(MPK_PATH, "rb")) {
        fread(g_mpk, 1, sizeof(g_mpk), f);
        fclose(f);
    }
}

static void mpk_flush() {
    if (FILE* f = fopen(MPK_PATH, "wb")) {
        fwrite(g_mpk, 1, sizeof(g_mpk), f);
        fclose(f);
    }
}

static uint8_t mpk_crc(const uint8_t* d) {
    uint8_t crc = 0;
    for (int i = 0; i <= 32; i++) {
        for (int m = 0x80; m != 0; m >>= 1) {
            uint8_t tap = (crc & 0x80) ? 0x85 : 0x00;
            crc <<= 1;
            if (i < 32 && (d[i] & m)) crc |= 1;
            crc ^= tap;
        }
    }
    return crc;
}

extern "C" void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx) {
    static uint8_t pifram[64];
    int32_t dir = (int32_t)ctx->r4;
    gpr dram = (gpr)(int32_t)ctx->r5;
    mpk_load();

    if (dir == 1) {
        for (int i = 0; i < 64; i++) pifram[i] = (uint8_t)MEM_BU(i, dram);
        bool empty = true;
        for (int i = 0; i < 32; i++) if (pifram[i]) { empty = false; break; }
        if (empty) {
            memset(pifram, 0, 64);
            for (int ch = 0; ch < 4; ch++) {
                uint8_t* p = &pifram[ch * 7];
                p[0] = 0xFF; p[1] = 0x01; p[2] = 0x03; p[3] = 0x00;
                if (ch == 0) { p[4] = 0x00; p[5] = 0x05; p[6] = 0x01; }
                else { p[2] = 0x03 | 0x80; }
            }
            pifram[28] = 0xFE;
        } else {
            int i = 0, ch = 0;
            while (i < 64) {
                uint8_t t = pifram[i];
                if (t == 0xFE) break;
                if (t == 0x00 || t == 0xFD || t == 0xFF) { i++; ch++; continue; }
                if (i + 1 >= 64) break;
                uint8_t tx = t & 0x3F;
                uint8_t rxn = pifram[i + 1] & 0x3F;
                uint8_t cmd = pifram[i + 2];
                uint8_t* rxp = &pifram[i + 2 + tx];
                bool serviced = false;
                if (cmd == 0x00 || cmd == 0xFF) {
                    if (ch == 0 && rxn >= 3) { rxp[0] = 0x00; rxp[1] = 0x05; rxp[2] = 0x01; serviced = true; }
                } else if (cmd == 0x02 && rxn >= 33) {
                    uint32_t addr = ((pifram[i + 3] << 8) | pifram[i + 4]) & 0xFFE0;
                    if (addr + 32 <= sizeof(g_mpk)) memcpy(rxp, &g_mpk[addr], 32);
                    else memset(rxp, 0, 32);
                    rxp[32] = mpk_crc(rxp);
                    serviced = true;
                } else if (cmd == 0x03 && tx >= 35) {
                    uint32_t addr = ((pifram[i + 3] << 8) | pifram[i + 4]) & 0xFFE0;
                    if (addr + 32 <= sizeof(g_mpk)) { memcpy(&g_mpk[addr], &pifram[i + 5], 32); mpk_flush(); }
                    if (rxn >= 1) rxp[0] = mpk_crc(&pifram[i + 5]);
                    serviced = true;
                }
                if (serviced) pifram[i + 1] &= ~0x80; else pifram[i + 1] |= 0x80;
                i += 2 + tx + rxn; ch++;
            }
        }
    } else {
        for (int i = 0; i < 64; i++) MEM_BU(i, dram) = (int8_t)pifram[i];
    }
    ultramodern::send_si_message();
    ctx->r2 = 0;
}
