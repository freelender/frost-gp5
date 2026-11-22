#ifndef FROST_HOT_CODE_H
#define FROST_HOT_CODE_H
/* FROST_HOT_RAM (staged target builds only): per-sample audio code goes into
 * .frost_hot and small per-sample lookup tables into .data. The staged linker
 * collects .frost_hot into .data, so the existing reset copy places both in
 * executable VARS RAM before any firmware runs (proven on the GP-5 in the NAM
 * placement research). Flash-resident code was measured as the dominant, spiky
 * cost. Host and default builds: no attributes, unchanged code. */
#if defined(FROST_HOT_RAM)
#if !defined(__arm__)
#error "FROST_HOT_RAM is a target-only placement"
#endif
#define FROST_HOT __attribute__((section(".frost_hot")))
#define FROST_HOT_TABLE __attribute__((section(".data.frost_tables")))
#else
#define FROST_HOT
#define FROST_HOT_TABLE
#endif
/* FROST_CORE (29a, GP-5 flash target only): the per-block audio path of the
 * core chain (I/O, stage-major loop, instance wrappers, settled loops, IR,
 * gate/EQ/DS-1 kernels, RV-6) shares one named section, so GCC emits it as
 * one contiguous block of flash. 28a5 showed that scattered hot loops evict
 * each other from the small flash cache (IR and DS-1 slowed sharply when both
 * ran). Placement only: code and arithmetic are unchanged. */
/* 30a: disabled. 29a (core placed ahead of the boot-sensitive region) left the
 * DSP silent from boot on the pedal; the tag stays as a no-op marker. */
#define FROST_CORE
/* FROST_DSP_RAM (30a, staged control-scheduler target only): the hottest
 * inner loops execute from VARS RAM, as upstream OpenGP5 recommends (its
 * Dreamcatcher firmware does this) and as the NAM placement research measured
 * (RAM kernels ~2x). RAM code cannot collide in the flash cache, the limit
 * 28a5 exposed. Same .data mechanism and reset dsb/isb as FROST_CTL_RAM.
 * The _NOFMA form keeps fp-contract=off for code written under that rule. */
#if defined(FROST_LARGE_CODE) && defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE) && !defined(FROST_HOT_RAM)
/* 31c: the 30b RAM loops (RV-6, plain reverb) join the LARGE code too. */
#define FROST_DSP_RAM __attribute__((section(".lcode"),noinline,optimize("O2","no-tree-loop-distribute-patterns")))
#define FROST_DSP_RAM_NOFMA __attribute__((section(".lcode"),noinline,optimize("O2","no-tree-loop-distribute-patterns","fp-contract=off")))
#elif defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE) && !defined(FROST_HOT_RAM)
#define FROST_DSP_RAM __attribute__((section(".data.frost_dsp"),noinline,optimize("O2","no-tree-loop-distribute-patterns")))
#define FROST_DSP_RAM_NOFMA __attribute__((section(".data.frost_dsp"),noinline,optimize("O2","no-tree-loop-distribute-patterns","fp-contract=off")))
#else
#define FROST_DSP_RAM
#define FROST_DSP_RAM_NOFMA
#endif
/* FROST_CTL_RAM (25c, staged control-scheduler target only): the slot-edit
 * control path executes from VARS RAM. It is placed in a .data.* section, so
 * the existing reset .data copy installs it; no linker change. On the GP-5 a
 * cold flash code line costs ~2.4k cycles (a 4.5k-instruction slot edit
 * measured 106k cycles while touching only 43 lines) and also evicts cached
 * audio code. Host/default builds: no attribute, unchanged code. */
#if defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE)
#define FROST_CTL_RAM __attribute__((section(".data.frost_ctl"),noinline,optimize("O2","no-tree-loop-distribute-patterns")))
#ifndef FROST_CTL_INLINE
#define FROST_CTL_INLINE __attribute__((always_inline))
#endif
#else
#define FROST_CTL_RAM
#ifndef FROST_CTL_INLINE
#define FROST_CTL_INLINE
#endif
#endif
/* Recall control loops have a measured cold-flash penalty on the pedal:
 * quick request > one audio period; silent commit > two. Keep their Os
 * instruction sequences, placing commit in VARS and validation in LARGE.
 * No work/memory-state change; target linker still enforces both RAM limits. */
#if defined(FROST_LARGE_CODE) && defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE)
#define FROST_RECALL_COMMIT_RAM __attribute__((section(".data.frost_ctl"),noinline))
#define FROST_RECALL_VALIDATE_RAM __attribute__((section(".lcode"),noinline))
#else
#define FROST_RECALL_COMMIT_RAM
#define FROST_RECALL_VALIDATE_RAM
#endif
/* FROST_COLD (27a, GP-5 target only): size-optimized code for rarely run or
 * non-critical paths, to bound DSP flash. Same C semantics (no fast-math; FP
 * contraction rules are unchanged by -Os). Host builds: empty. */
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__GNUC__)
#define FROST_COLD __attribute__((optimize("Os")))
#else
#define FROST_COLD
#endif
/* FROST_OPAQUE (28a, GP-5 target only): an empty asm that hides a float's
 * origin from the optimiser, so a kernel inlined into a loop cannot have its
 * final multiply fused (FMA) with the caller's mix: rounding stays exactly as
 * when the kernel returned through a call. Emits no instruction. */
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__GNUC__)
#define FROST_OPAQUE(v) __asm__("" : "+t"(v))
#else
#define FROST_OPAQUE(v) ((void)0)
#endif
/* FROST_NOFMA (31a, GP-5 target): no fused multiply-add, so the FFT IR's
 * rounding matches the host build (-ffp-contract=off) exactly. On the M4 a
 * VFMA costs 3 cycles, VMUL+VADD 2. Host: empty (already contract-off). */
#if defined(__arm__) && defined(GP5_FIRMWARE) && defined(__GNUC__)
#define FROST_NOFMA __attribute__((optimize("O2","no-tree-loop-distribute-patterns","fp-contract=off")))
#else
#define FROST_NOFMA
#endif
/* FROST_AUDIO_RAM / FROST_AUDIO_RAM_NOFMA (31b): the steady per-block audio
 * path (FFT cabinet, settled loops, DS-1, chain/instance block drivers, plain
 * reverb block, delay) executes from VARS RAM, with all of its data in LARGE
 * (the chain moved there). 30a measured RAM code + LARGE data fast (RV-6
 * -30%) and RAM code + VARS data slow; 31a showed flash code costs dominated
 * by cache layout. Placement only: same arithmetic (NOFMA where the host
 * identity needs it). Host and non-scheduler builds: no attribute (NOFMA kept). */
/* 31c (FROST_LARGE_CODE, staged): the same functions execute from LARGE
 * (.lcode, copied by c_start), the Code-region bus where stock runs its effect
 * modules; 31b measured VARS code at a uniform ~4 cycles/instruction. */
#if defined(FROST_LARGE_CODE) && defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE) && !defined(FROST_HOT_RAM)
#define FROST_AUDIO_RAM __attribute__((section(".lcode"),noinline,optimize("O2","no-tree-loop-distribute-patterns")))
#define FROST_AUDIO_RAM_NOFMA __attribute__((section(".lcode"),noinline,optimize("O2","no-tree-loop-distribute-patterns","fp-contract=off")))
#elif defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE) && !defined(FROST_HOT_RAM)
#define FROST_AUDIO_RAM FROST_DSP_RAM
#define FROST_AUDIO_RAM_NOFMA FROST_DSP_RAM_NOFMA
#else
#define FROST_AUDIO_RAM
#define FROST_AUDIO_RAM_NOFMA FROST_NOFMA
#endif
/* FROST_LARGE_ONLY (31d, FROST_LARGE_CODE target): section placement only,
 * no optimize/noinline change, so the moved library kernels keep their exact
 * code; they execute from LARGE like the 31c audio path. Else empty. */
#if defined(FROST_LARGE_CODE) && defined(FROST_CONTROL_SCHEDULER) && defined(__arm__) && defined(GP5_FIRMWARE) && !defined(FROST_HOT_RAM)
#define FROST_LARGE_ONLY __attribute__((section(".lcode")))
#else
#define FROST_LARGE_ONLY
#endif
/* FROST_OVL(id) (31i, FROST_OVERLAYS staged target): the function belongs to
 * overlay module <id>, linked in dsp-data flash and copied into the LARGE
 * overlay pool when a route uses it. Placement only. Else empty. */
#if defined(FROST_OVERLAYS) && defined(FROST_LARGE_CODE) && defined(__arm__) && defined(GP5_FIRMWARE)
#define FROST_OVL(id) __attribute__((section(".ovl." #id)))
#else
#define FROST_OVL(id)
#endif
/* Keep shared helpers out of line to bound DSP flash; no arithmetic change. */
#if defined(FROST_REVERB_INT16) && defined(__arm__) && defined(GP5_FIRMWARE)
/* The compact backend retains the old callers of boot-region statics. Their
 * unused float loops belong after boot in flash, rather than consuming LARGE. */
#define FROST_REVERB_FLOAT_AUDIO __attribute__((section(".frost_legacy_reverb_code"),noinline,optimize("O2","no-tree-loop-distribute-patterns")))
#define FROST_REVERB_FLOAT_DSP FROST_REVERB_FLOAT_AUDIO
#define FROST_REVERB_FLOAT_DSP_NOFMA __attribute__((section(".frost_legacy_reverb_code"),noinline,optimize("O2","no-tree-loop-distribute-patterns","fp-contract=off")))
#else
#define FROST_REVERB_FLOAT_AUDIO FROST_AUDIO_RAM
#define FROST_REVERB_FLOAT_DSP FROST_DSP_RAM
#define FROST_REVERB_FLOAT_DSP_NOFMA FROST_DSP_RAM_NOFMA
#endif
#if defined(__GNUC__)
#define FROST_NOINLINE __attribute__((noinline))
#else
#define FROST_NOINLINE
#endif
/* FROST_UNROLL(n) (31o): full unroll of a constant-trip loop on the GCC
 * target only; code motion only, same arithmetic. No-op elsewhere. */
#if defined(__GNUC__) && !defined(__clang__)
#define FROST_UNROLL_STR(x) #x
#define FROST_UNROLL(n) _Pragma(FROST_UNROLL_STR(GCC unroll n))
#else
#define FROST_UNROLL(n)
#endif
#endif
