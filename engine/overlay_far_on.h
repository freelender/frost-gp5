/* 31i overlays (FROST_OVERLAYS, staged target only): inside overlay module
 * code, shared out-of-line helpers are called through RAM function pointers.
 * An overlay runs from a copy at a different address than it is linked at, so
 * it may not use PC-relative calls to code outside itself; a pointer call is
 * absolute. The callee is the same out-of-line function, so arithmetic is
 * unchanged. Inline wrappers that call these helpers are restated with the
 * same expression and float conversions. Re-includable; pair with
 * overlay_far_off.h. Host and other builds: no effect. */
#ifdef FROST_OVERLAYS
#define frost_soft_clip(x) (frost_far_soft_clip(x))
#define frost_exp2(x) (frost_far_exp2(x))
#define frost_log2(x) (frost_far_log2(x))
#define frost_sin01(x) (frost_far_sin01(x))
#define frost_allpass_coef(x) (frost_far_allpass_coef(x))
#define frost_taper_log(k,lo,r) ((float)(lo)*frost_far_exp2((float)(k)*0.01f*(float)(r)))
#define frost_db_to_gain(db) (frost_far_exp2((float)(db)*0.16609640f))
#define frost_gain_to_db(g) (6.02059991f*frost_far_log2((float)(g)))
#define frost_half_load(h) (frost_half_load_inl(h))
#define frost_half_store(x) (frost_half_store_inl(x))
#endif
