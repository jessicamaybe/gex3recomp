#ifndef __PATCHES_H__
#define __PATCHES_H__

// RECOMP_PATCH-style attributes, matching Zelda64Recomp's mechanism. Functions
// tagged RECOMP_PATCH land in the .recomp_patch section; N64Recomp's patch-mode
// recompiles them and the runtime (recomp::overlays::register_patches) overrides
// the original game function of the same name. No ROM bytes are modified.
#define RECOMP_EXPORT       __attribute__((section(".recomp_export")))
#define RECOMP_PATCH        __attribute__((section(".recomp_patch")))
#define RECOMP_FORCE_PATCH  __attribute__((section(".recomp_force_patch")))

#endif
