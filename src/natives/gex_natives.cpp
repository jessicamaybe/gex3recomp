// Game-specific native replacements for Gex 3.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <tuple>
#include <unordered_map>

#include <librecomp/addresses.hpp>
#include <librecomp/game.hpp>
#include <ultramodern/ultramodern.hpp>

#include "recomp.h"

extern "C" void load_overlays(uint32_t rom, int32_t ram_addr, uint32_t size);
extern "C" void unload_overlays(int32_t ram_addr, uint32_t size);

// ---------------------------------------------------------------------------
// Compressed scene-class modules (see ARCHITECTURE.md §8e)
//
// Scene class modules ("alfred", "rezntro", ...) are LZ-compressed TOC assets.
// The engine stages each one with chunked DMAs to 0x801E0A70 and decompresses
// it to a per-scene module slot (e.g. 0x80113910). The code only ever exists
// decompressed in RAM, so N64Recomp can't see it in the ROM. Pipeline:
//   1. func_80031E70 (below) tracks the staging DMAs (coalescing chunks).
//   2. gex3_module_loaded — called from a patch in func_800313DC right after
//      func_80031128 returns the decompress destination (see fix_modhook.py):
//        - manifest hit  -> activate the matching recompiled section
//          (gen_syms.py appended the extracted image to gex3_shadow.bin and
//          emitted an overlay section for it; xmods_manifest.txt maps
//          (compressed_rom, dest_vram) -> (size, shadow_rom_offset))
//        - manifest miss -> dump the decompressed image to extracted_mods/
//          and log it to extracted_pending.txt; the next ./cycle.sh folds it
//          into the shadow input and recompiles it.
// ---------------------------------------------------------------------------

static const uint32_t STAGING_RAM = 0x801E0A70;

// current coalesced staging run (loader thread only)
static uint32_t g_staged_rom = 0;       // first chunk's rom offset
static uint32_t g_staged_end = 0;       // rom offset just past the last chunk

struct XmodEntry { uint32_t size, shadow_rom; };
static std::unordered_map<uint64_t, XmodEntry> g_xmods;
static bool g_xmods_loaded = false;

static void load_xmods_manifest() {
    if (g_xmods_loaded) return;
    g_xmods_loaded = true;
    FILE* f = fopen("xmods_manifest.txt", "r");
    if (!f) return;
    uint32_t rom, dest, size, shadow;
    while (fscanf(f, "%x %x %x %x", &rom, &dest, &size, &shadow) == 4) {
        g_xmods[((uint64_t)rom << 32) | dest] = {size, shadow};
    }
    fclose(f);
    fprintf(stderr, "[xmod] manifest: %zu entries\n", g_xmods.size());
}

extern "C" void gex3_module_loaded(uint8_t* rdram, recomp_context* ctx) {
    uint32_t dest = (uint32_t)ctx->r2;
    uint32_t rom = g_staged_rom;
    uint32_t comp_size = g_staged_end - g_staged_rom;
    g_staged_rom = g_staged_end = 0;  // consume the staging run
    if (dest == 0 || rom == 0) return;

    load_xmods_manifest();
    auto it = g_xmods.find(((uint64_t)rom << 32) | dest);
    if (it != g_xmods.end()) {
        if (it->second.size != 0) {  // size 0 = known data-only asset
            unload_overlays((int32_t)dest, it->second.size);
            load_overlays(it->second.shadow_rom, (int32_t)dest, it->second.size);
            fprintf(stderr, "[xmod] activated rom 0x%06X at 0x%08X size 0x%X\n",
                    rom, dest, it->second.size);
        }
        return;
    }

    // unknown module: extract for the next recompile cycle
    static std::set<std::pair<uint32_t, uint32_t>> dumped;
    if (!dumped.emplace(rom, dest).second) return;
    char path[128];
    snprintf(path, sizeof(path), "extracted_mods/x_%08X_%08X.bin", rom, dest);
    if (FILE* f = fopen(path, "rb")) { fclose(f); return; }  // from an earlier run
    // LZ here decompresses to roughly 2-4x; 8x bounds the dump without
    // swallowing whole neighboring regions (gen_syms trims to the code span)
    uint32_t DUMP = 8 * comp_size;
    if (DUMP < 0x2000) DUMP = 0x2000;
    if (DUMP > 0x20000) DUMP = 0x20000;
    if (FILE* f = fopen(path, "wb")) {
        fwrite(rdram + (dest & 0x3FFFFFF), 1, DUMP, f);
        fclose(f);
        if (FILE* p = fopen("extracted_pending.txt", "a")) {
            fprintf(p, "0x%X 0x%X\n", rom, dest);
            fclose(p);
        }
        fprintf(stderr, "[xmod] extracted rom 0x%06X at 0x%08X -> %s (run ./cycle.sh)\n",
                rom, dest, path);
    }
}

// Gex 3's synchronous code-DMA helper: func_80031E70(rom_offset, dramAddr, size).
// Every engine code load (boot blob, per-level overlays, streamed entity code)
// funnels through here, so this is the one place the runtime's overlay function
// tables need to be kept in sync with what the engine loads. Unknown blobs are
// recorded to discovered_blobs.txt so gen_syms.py can recompile them next cycle.
extern "C" void func_80031E70_recomp(uint8_t* rdram, recomp_context* ctx) {
    uint32_t rom = (uint32_t)ctx->r4;
    gpr ram = (gpr)(int32_t)ctx->r5;
    uint32_t size = (uint32_t)ctx->r6;
    fprintf(stderr, "[loader] code dma rom 0x%06X -> ram 0x%08X size 0x%X\n",
            rom, (uint32_t)ram, size);
    if ((uint32_t)ram == STAGING_RAM) {
        // staging chunk for a compressed asset: coalesce consecutive chunks
        if (rom != g_staged_end || g_staged_rom == 0) g_staged_rom = rom;
        g_staged_end = rom + size;
    }
    static std::set<std::tuple<uint32_t, uint32_t, uint32_t>> seen;
    static const bool trace = getenv("GEX3_BLOBS") != nullptr;
    if (seen.emplace(rom, (uint32_t)ram, size).second && trace) {
        if (FILE* f = fopen("discovered_blobs.txt", "a")) {
            fprintf(f, "0x%X 0x%X 0x%X\n", rom, (uint32_t)ram, size);
            fclose(f);
        }
    }
    // The engine reuses fixed bases (module slot 0x80113910, staging, ...) for
    // differently-sized images. librecomp refuses partial unloads, so unload
    // the full extent of whatever was last loaded at this base, not just the
    // incoming DMA's size ("Cannot partially unload section" abort otherwise).
    static std::unordered_map<uint32_t, uint32_t> loaded_extent;
    uint32_t prev = loaded_extent[(uint32_t)ram];
    unload_overlays((int32_t)ram, size > prev ? size : prev);
    recomp::do_rom_read(rdram, ram, recomp::rom_base | rom, size);
    load_overlays(rom, (int32_t)ram, size);
    loaded_extent[(uint32_t)ram] = size;
    ctx->r2 = 0;
}

// Some call sites are emitted with the plain name — route them to the native.
extern "C" void func_80031E70(uint8_t* rdram, recomp_context* ctx) {
    func_80031E70_recomp(rdram, ctx);
}
