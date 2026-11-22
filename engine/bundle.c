/* Single-translation-unit bridge for OpenGP5's header-based DSP adapter.
 * Ordinary host/CMake builds compile modules separately instead. */
#include "../effects/gain/gain.c"
#include "../effects/noise_gate/noise_gate.c"
#include "../effects/eq/eq.c"
#include "../effects/cabinet_ir/ir.c"
#include "../effects/overdrive/overdrive.c"
#include "../effects/delay/delay.c"
#include "../effects/modulation/modulation.c"
#include "../effects/chorus/chorus.c"
#include "../effects/flanger/flanger.c"
#include "../effects/reverb/reverb.c"
#include "effect.c"
#include "chain.c"
#include "preset.c"
/* Pedal-inspired model kernels are emitted AFTER all original code (the GP-5
 * build includes bundle_library.c at the very end of the DSP translation
 * unit). Placing ~14 KB of kernels between the original kernels and the
 * engine slowed the flash-resident hot path and command handlers on the
 * pedal (revision 23 measurement); this restores the revision 22 layout. */
#ifndef FROST_LIBRARY_KERNELS_LATE
#include "bundle_library.c"
#endif
