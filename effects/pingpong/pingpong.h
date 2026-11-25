#ifndef FROST_PINGPONG_H
#define FROST_PINGPONG_H
#include "../delay/delay.h"
/* PING: stereo ping-pong delay (beta.14). Uses the shared delay workspace
 * (one delay-class block at a time) and the stereo side signal
 * (engine/stereo.h). Parameters: TIME ms, F.BACK, E.LEVEL, WIDTH, TAIL. */
#define FROST_PINGPONG_MAX_TIME 400.0f      /* two hops must fit the 800 ms ring */
typedef struct { FrostDelay *ring; float distance,lp[2]; } FrostPingPong;
void frost_pingpong_reset(FrostPingPong *s);
float frost_pingpong_sample(FrostPingPong *s,float x,const float *p);
#endif
