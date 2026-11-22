/* Pedal-inspired model kernels, included last (see bundle.c). Kernels are
 * FROST_NOINLINE so they are not inlined into earlier registry wrappers. */
#include "../effects/common/frost_dsp.c"
#include "../effects/drives/drives.c"
#include "../effects/dynamics/dynamics.c"
#include "../effects/modulators/modulators.c"
#include "../effects/octave/octave.c"
#include "../effects/drop_tune/drop_tune.c"
#include "../effects/fcm/fcm_effect.c"
#include "../effects/fcm/fcm_transfer.c"
#include "../effects/delays/delays.c"
#include "../effects/reverbs/reverbs.c"
#ifdef FROST_REVERB_INT16
#include "../effects/reverb/reverb16.c"
#include "../effects/reverbs/reverbs16.c"
/* Keep the original public kernels reachable so the boot-sensitive statics
 * retain exactly their original callers and code. Never anchor statics. */
#if defined(__arm__) && defined(GP5_FIRMWARE)
__attribute__((used,section(".frost_legacy_reverb")))
const void *const frost_legacy_reverb_anchors[]={
    (const void *)frost_rv6_block,(const void *)frost_spring_sample};
#endif
#endif

#include "../effects/ns1x/ns1x.c"
#include "../effects/multiband/multiband.c"
#include "../effects/highpass/highpass.c"
#include "../effects/lowpass/lowpass.c"
#include "../effects/autowah/autowah.c"
#include "../effects/treble_boost/treble_boost.c"
#include "../effects/foxx/foxx.c"
#include "../effects/tape/tape.c"
#include "../effects/vintage_fuzz/vintage_fuzz.c"
#include "../effects/acoustic/acoustic.c"
#include "../effects/amp_heads/amp_heads.c"
