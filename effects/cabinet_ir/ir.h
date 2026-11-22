#ifndef FROST_IR_H
#define FROST_IR_H
#include <stdint.h>
#define FROST_IR_MAX 512u
#ifndef FROST_IR_LOCAL_TAPS
#define FROST_IR_LOCAL_TAPS 128u   /* stage 4: DSP fallback length with the IR link */
#endif
/* 31a: uniform-partitioned overlap-save FFT convolution. 64-sample partitions,
 * 128-point real FFTs, zero added latency (the partition equals the 64-sample
 * platform block). Optional caller-owned workspace; without one the IR runs
 * the direct-form paths unchanged. FROST_IR_FFT_PARTS bounds the IR length the
 * FFT path serves (64 taps per partition); longer IRs stay direct-form. */
#define FROST_IR_FFT_BLOCK 64u
#define FROST_IR_FFT_SIZE 128u
#ifndef FROST_IR_FFT_PARTS
#define FROST_IR_FFT_PARTS 4u
#endif
typedef struct {
#ifdef FROST_IR_FFT_SPLIT_SPECTRUM
    /* 31l: caller-owned filter spectra can live in LARGE while the input FDL
     * and working buffers stay in VARS. Set before binding; keep alive and
     * exclusive until unbound. Eight partitions then add only a pointer to
     * the original four-part workspace's VARS footprint. */
    float (*spectrum)[FROST_IR_FFT_SIZE];
#else
    float spectrum[FROST_IR_FFT_PARTS][FROST_IR_FFT_SIZE]; /* filter partitions, packed half spectra x 1/128 */
#endif
    float input[FROST_IR_FFT_PARTS][FROST_IR_FFT_SIZE];    /* frequency-domain delay line of input windows */
    float frame[FROST_IR_FFT_SIZE];   /* [previous 64 inputs, current (partial) 64] */
    float scratch[FROST_IR_FFT_SIZE];
    float twiddle[FROST_IR_FFT_SIZE]; /* W128^k = (cos, -sin), k = 0..63 */
    unsigned parts,head,fill,dirty; /* dirty: FDL/frame hold data since the last clear */
} FrostIRFFT;
typedef struct { float coefficients[FROST_IR_MAX],history[FROST_IR_MAX];unsigned taps,position; } FrostIR;
/* The FrostIR layout is unchanged since 30b; workspaces attach through a small
 * registry keyed by the FrostIR address (the target binds one). */
#ifndef FROST_IR_FFT_BINDINGS
#if defined(__arm__) && defined(GP5_FIRMWARE)
#define FROST_IR_FFT_BINDINGS 1u
#else
#define FROST_IR_FFT_BINDINGS 8u
#endif
#endif
/* Attach (or detach with NULL) an exclusive workspace, then recompute the
 * spectra of the current coefficients and clear all histories. Cold only.
 * Returns 0 when the registry is full (nothing changed). Split-spectrum
 * builds also reject a workspace with no spectrum pointer, without mutation. */
int frost_ir_bind_fft(FrostIR *s,FrostIRFFT *workspace);
/* The attached workspace, or NULL. */
FrostIRFFT *frost_ir_fft_of(const FrostIR *s);
/* Non-NULL only inside the FFT block's per-sample replay: frost_ir_sample then
 * returns the next precomputed output instead of convolving. */
extern const float *frost_ir_fft_replay;
extern unsigned frost_ir_generation;
/* Clear only the FFT histories (no-op without a workspace). */
void frost_ir_fft_reset(const FrostIR *s);
/* True when the next 64 inputs can take the FFT block path. */
int frost_ir_fft_ready(const FrostIR *s);
/* Exactly FROST_IR_FFT_BLOCK raw FIR outputs (no level/mix); input/output may
 * alias. Requires frost_ir_fft_ready. Also advances the direct-form history. */
void frost_ir_fft_block(FrostIR *s,const float *input,float *output);
/* Clear histories (time ring and FFT state) without touching coefficients. */
void frost_ir_clear(FrostIR *s);
/* Load at an audio-thread safe boundary. Caller coefficients must not alias s.
 * Load resets the tail; live IR replacement/crossfade is not implemented. */
/* Keeps an attached FFT workspace and rebuilds its spectra (P forward FFTs). */
int frost_ir_load(FrostIR *s,const float *coefficients,unsigned taps);
/* 31a: coefficients q[i]*scale (exact float products); same validation,
 * atomic on failure. Lets a baked table use 2 bytes per tap. */
int frost_ir_load_q15(FrostIR *s,const int16_t *q,unsigned taps,float scale);
float frost_ir_sample(FrostIR *s,float input);
/* Two consecutive samples, same coefficient/accumulation order per output.
 * Reuses each coefficient load with independent accumulators. Writes the
 * second input to history only after both sums, retaining output0's oldest
 * history sample. Output pointers must be distinct and not alias s. */
void frost_ir_pair(FrostIR *s,float input0,float input1,float *output0,float *output1);
/* Block form (25a): requires frost_ir_block_fits (taps+count<=FROST_IR_MAX).
 * Raw FIR sums for count consecutive inputs, bit-identical to frost_ir_sample;
 * input/output may alias. Leaves position=0 with chronological history. */
int frost_ir_block_fits(const FrostIR *s,unsigned count);
void frost_ir_block(FrostIR *s,const float *input,float *output,unsigned count);
/* Kernels of frost_ir_block, exposed for instrumentation tests only. */
void frost_ir_dot8(const float *c,const float *h,unsigned taps,float *out);
float frost_ir_dot1(const float *c,const float *h,unsigned taps);
#endif
