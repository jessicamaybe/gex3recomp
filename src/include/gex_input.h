#ifndef GEX_INPUT_H
#define GEX_INPUT_H

#include <cstdint>
#include <ultramodern/input.hpp>
#include "recomp.h"

namespace gex {

void input_poll();
bool input_get(int n, uint16_t* buttons, float* x, float* y);
void input_rumble(int n, bool rumble);
ultramodern::input::connected_device_info_t input_device_info(int n);

} // namespace gex

extern "C" {
void __osSiRawStartDma_recomp(uint8_t* rdram, recomp_context* ctx);
}

#endif // GEX_INPUT_H
