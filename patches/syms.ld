__start = 0x80000000;

/* Host-provided callbacks get dummy absolute addresses here that the recompiler
 * turns into calls to registered base exports. None are needed for the initial
 * culling patches; add entries (e.g. recomp_get_target_aspect_ratio = 0x8F0000xx)
 * as patches start calling host functions. */
