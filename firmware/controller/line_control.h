#ifndef FROST_LINE_CONTROL_H
#define FROST_LINE_CONTROL_H
/* Simple line test: VOL sets the requested level directly. The platform already
 * suppresses its 60 filter-start readings and only sends a level after DSP sync.
 * Readings <=2/255 are treated as zero. The DSP enforces the hard cap, ramp and
 * fault silence independently of this value. */
typedef struct {unsigned level;} FrostLineControl;
static void frost_line_control_init(FrostLineControl *s){s->level=0;}
static void frost_line_control_knob(FrostLineControl *s,unsigned knob)
{s->level=knob<=2u || knob>255u?0:knob;}
#endif
