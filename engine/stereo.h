#ifndef FROST_STEREO_H
#define FROST_STEREO_H
/* beta.14 stereo: a mono chain plus a "side" signal.
 *
 * Every block still processes one mono signal (the mid). A stereo effect also
 * writes the left/right difference for each sample it renders into
 * frost_side[] (frost_side_write); the output stage then sends mid + side to
 * the left output and mid - side to the right. Without a stereo effect, or in
 * MONO output mode, nothing is written and the output is exactly as before, so
 * stereo costs no DSP time unless it is in use.
 *
 * Effects after a stereo block only see the mid. That is right for delays,
 * reverbs and modulation (the spread passes around them), but an amp, drive,
 * cab, pitch or dynamics block after it would leave the side part unprocessed,
 * so the chain folds the effect back to mono then (frost_side_route_ok = 0,
 * decided at a route commit, engine/chain.c). The chain also scales the side
 * signal by the stereo block's on/off fade (TAIL OFF) and by route fades.
 *
 * Kernels write one side sample per rendered sample, in order; the chain
 * resets the position at the start of every block (FROST_SIDE_BLOCK samples
 * at most on the pedal). */
#include "metadata.h"
#define FROST_SIDE_BLOCK 64u
/* The block's side samples. On the pedal fw_process points this at a buffer on
 * its own stack each block (the DSP's static RAM has no room for 256 bytes);
 * host builds use a static buffer. Valid only while a block is processed. */
extern float *frost_side;
extern unsigned frost_side_pos,frost_side_used;
extern unsigned frost_stereo_output;     /* OUTPUT MODE: 1 stereo (global setting bit 0) */
extern unsigned frost_side_route_ok;     /* 0: a block after the stereo effect needs mono */
extern unsigned frost_side_slot;         /* slot index of the stereo block, >= FROST_SLOTS if none */
static inline unsigned frost_type_is_stereo(unsigned type){return type==FROST_PINGPONG;}
static inline unsigned frost_side_on(void){return frost_stereo_output && frost_side_route_ok;}
static inline void frost_side_begin(void){frost_side_pos=0;frost_side_used=0;}
static inline void frost_side_write(float v)
{frost_side[frost_side_pos&(FROST_SIDE_BLOCK-1u)]=v;++frost_side_pos;frost_side_used=1;}
#endif
