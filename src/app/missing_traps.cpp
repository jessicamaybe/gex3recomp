#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "recomp.h"

extern "C" {

#define TRAP(n) void n(uint8_t* rdram, recomp_context* ctx) { \
    fprintf(stderr, "[TRAP] %s\n", #n); abort(); }

TRAP(__osDequeueThread_recomp)
TRAP(__osEnqueueAndYield_recomp)
TRAP(__osInsertTimer_recomp)
TRAP(func_8007AFC8)
TRAP(func_80073470)
TRAP(func_80073600)
TRAP(__osDpDeviceBusy_recomp)
TRAP(__osEPiRawWriteIo_recomp)
TRAP(__osEPiRawReadIo_recomp)
TRAP(__osSpDeviceBusy_recomp)

}
