#ifndef FROST_CHAIN_H
#define FROST_CHAIN_H
#include "effect.h"
#include "preset.h"
/* Mono 44.1 kHz. Caller-owned, no heap. All commands and processing execute
 * on the audio thread at block boundaries; no concurrent setters. */
#define FROST_SLOTS FROST_SLOT_COUNT
#define FROST_IR_WORKSPACES FROST_PRESET_IRS
#define FROST_ROUTE_FADE 128u
enum { FROST_ROUTE_IDLE, FROST_ROUTE_OUT, FROST_ROUTE_IN, FROST_ROUTE_COMMIT, FROST_ROUTE_QUIET, FROST_ROUTE_WARM };
/* 31y14: with frost_chain_commit_alone set (target firmware), a finished
 * fade-out does not commit inside the same block; the next block is silent,
 * runs no slot, only commits (cold flash code: resets, overlays), then the
 * fade-in starts. Pedal: the commit block reached 586k cycles (213%) after a
 * route change. Default 0 keeps the original semantics. */
extern unsigned frost_chain_commit_alone;
/* Optional caller-owned amp boundary. These callbacks must be bounded,
 * nonblocking and allocation-free. NULL means no amp, not a NAM substitute.
 * Pass the same stage/context through all chunks of a stream/transition. */
typedef struct {
    float (*sample)(void *state,float input);
    void (*reset)(void *state);
    void *state;
} FrostAmpStage;
typedef struct {
    FrostInstance slots[FROST_SLOTS];
    FrostIR ir_pool[FROST_IR_WORKSPACES];
    FrostDelay *delay_workspace;
    FrostModulation *modulation_pool;
    FrostReverbWorkspace *reverb_workspace;
    uint32_t faults;
    unsigned started;
    unsigned order[FROST_SLOTS], pre_count;
    unsigned pending_types[FROST_SLOTS], pending_order[FROST_SLOTS], pending_pre_count;
    unsigned transition, fade_position;
    uint32_t recall_token; /* negotiated pre-fade owner; zero for ordinary routes */
    FrostPreset pending_preset;
    unsigned pending_preset_active;
    unsigned pending_policy_slot,pending_policy_value;
} FrostChain;
/* Contains internal workspace pointers: never memcpy/move an initialized
 * chain. Initialize each chain at its final address. */
void frost_chain_init(FrostChain *s);
/* Cold-only, before delay types are configured. Exclusive caller-owned storage
 * must outlive this chain; hardware places it in GP5_LARGE, not ordinary VARS.
 * NULL disables admission. Init detaches it. No initialized chain copies. */
int frost_chain_bind_delay(FrostChain *s,FrostDelay *workspace);
/* Cold-only, before chorus/flanger configuration. Pool must contain exactly
 * FROST_MOD_WORKSPACES exclusive objects, at final addresses, outliving chain.
 * NULL disables admission; init detaches it. Pool cannot be shared by chains. */
int frost_chain_bind_modulation(FrostChain *s,FrostModulation *pool);
/* 31a, cold-only: attach FFT workspaces to the first count IR workspaces
 * (count 0 detaches all), recomputing spectra and clearing IR histories.
 * Exclusive caller-owned storage at final addresses, outliving the chain;
 * hardware places it in GP5_LARGE. Unbound IRs keep the direct form. */
int frost_chain_bind_ir_fft(FrostChain *s,FrostIRFFT *pool,unsigned count);
/* Cold-only, before reverb configuration; exclusive caller-owned storage at
 * final address, outliving the chain. NULL disables admission; init detaches. */
int frost_chain_bind_reverb(FrostChain *s,FrostReverbWorkspace *workspace);
/* Cold configuration only: rejected after the first nonempty process call.
 * Slots run in ascending order, including duplicate types with separate state.
 * At most two IRs. Cold routes use slot order and put all slots before amp. */
int frost_chain_configure(FrostChain *s,const unsigned *types,unsigned count);
/* Fixed-size arrays: six types and a permutation of slot IDs 0..5.
 * Rejected atomically if invalid/busy. The caller need not retain arrays.
 * Fade out for 128 frames, commit/reset histories at silence, fade in for 128.
 * Unchanged types retain parameters/enable/IR coefficients. New types start
 * at defaults, bypassed; new IRs use identity coefficients. Tails are cut.
 * This engine API does not itself notify a protocol revision; protocol owners
 * must use frost_slots_request_route instead when sharing a protocol state. */
int frost_chain_request_route(FrostChain *s,const unsigned *types,const unsigned *order,unsigned pre_count);
unsigned frost_chain_transition(const FrostChain *s);
/* Control-thread snapshot/recall, serialized with setters/audio. Capture target
 * values/assets/policies, not smoothed values or histories. Caller provides a
 * distinct output object; busy/null rejects before writing. Snapshot may hold
 * private assets, so keep generated captures out of public source.
 * Request validates/copies all fields before scheduling, even for identical
 * settings. At silence it restores all targets/current values/enables/policies
 * and IR coefficients, resets histories, then fades in. No spillover. Caller
 * can release/edit input after acceptance; no pointers retained. */
int frost_chain_capture_preset(const FrostChain *s,FrostPreset *out);
int frost_chain_request_preset(FrostChain *s,const FrostPreset *preset);
/* Legacy IDs 0..3 address gain/gate/EQ/IR in the default layout.
 * Generic callers address slots, not effect types. */
int frost_chain_set(FrostChain *s,unsigned slot,unsigned parameter,float value);
int frost_chain_enable(FrostChain *s,unsigned slot,unsigned enabled);
/* Cold-only slot policy, including empty slots reserved for future types.
 * Configure types first, then policies before the first nonempty process call.
 * Default KEEP preserves original exponential bypass/history behavior.
 * CUT uses a reversible 128-valid-sample ramp, resets at dry and skips sample
 * callbacks while suspended. Parameters still smooth. Live type replacement
 * retains the slot's policy; cold configure resets policies to KEEP.
 * Native presets and0x7f45 carry the policy; this setter remains cold-only. */
int frost_chain_set_bypass_policy(FrostChain *s,unsigned slot,unsigned policy);
/* Live policy change through global route silence/reset. Same-value is a no-op.
 * Retains targets/assets/order/pre boundary. Serialized control owner must fence
 * revisions. Genuine changes cut all histories at silence, not spillover. */
int frost_chain_request_bypass_policy(FrostChain *s,unsigned slot,unsigned policy);
/* Resetting replacement: muted diagnostic/offline use only. */
int frost_chain_load_ir(FrostChain *s,unsigned slot,const float *coefficients,unsigned taps);
int frost_chain_request_ir_bypass(FrostChain *s,unsigned on);
extern unsigned frost_ir_bypass_pending;
#ifdef FROST_IR_LINK
extern unsigned frost_ir_remote,frost_ir_remote_pending;
extern uint32_t frost_ir_remote_cycles;
extern const float *(*frost_ir_remote_fn)(FrostIR *ir,const float *x);
int frost_chain_request_ir_remote(FrostChain *s,unsigned on);
#endif
/* 31a: same, from 16-bit coefficients q[i]*scale (a baked cabinet table). */
int frost_chain_load_ir_q15(FrostChain *s,unsigned slot,const int16_t *q,unsigned taps,float scale);
/* Input and every effect/amp boundary must be finite and within +/-16.
 * A bad frame skips remaining stages, resets all histories, increments the
 * saturating fault counter and outputs silence; transitions still advance.
 * This containment limit is not a unity-level/output headroom guarantee.
 * Exact in-place supported; partial overlap unsupported. Work bounded by
 * count * six slots * registered per-sample cost (IR <= 512 taps). */
void frost_chain_process(FrostChain *s,const float *input,float *output,unsigned count);
void frost_chain_process_with_amp(FrostChain *s,const float *input,float *output,unsigned count,const FrostAmpStage *amp);
/* Optional fatal-stream policy: identical valid processing, but the first bad
 * input/stage/amp result resets histories once, counts one saturating fault,
 * silences the ENTIRE block and returns0. No remaining kernels/transitions run.
 * Returns1 on success/empty call. In-place allowed. Caller must stop/recover
 * the stream on0; this function alone does not latch across calls. No heap/IO. */
int frost_chain_process_guarded(FrostChain *s,const float *input,float *output,unsigned count,const FrostAmpStage *amp);
#endif
