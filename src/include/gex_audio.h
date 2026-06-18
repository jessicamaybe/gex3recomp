#ifndef GEX_AUDIO_H
#define GEX_AUDIO_H

#include <cstdint>
#include <cstddef>
#include <librecomp/rsp.hpp>

namespace gex {

void audio_queue_samples(int16_t* samples, size_t count);
size_t audio_frames_remaining();
void audio_set_frequency(uint32_t freq);

} // namespace gex

extern "C" {
void gex_voice_reset(void);
int gex_voice_skip(void);
void gex_fill_window(uint8_t* rdram, uint32_t desc, uint32_t addr);
void asp_cmd_snapshot(uint8_t* rdram, uint32_t w0, uint32_t w1);
void asp_dump_decode(uint8_t* rdram, uint32_t state_dmem, uint32_t state_ram);
RspExitReason asp_logged(uint8_t* rdram, uint32_t ucode_addr);
}

#endif // GEX_AUDIO_H
