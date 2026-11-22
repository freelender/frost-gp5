#ifndef FROST_KERNEL_BLOCK_H
#define FROST_KERNEL_BLOCK_H
/* Stateful block kernel. p is constant over the block; p_start optionally
 * carries the chunk's initial controls for kernels that interpolate them. */
typedef void (*FrostBlockFn)(void *state,const float *x,float *y,unsigned n,const float *p,const float *p_start);
#endif
