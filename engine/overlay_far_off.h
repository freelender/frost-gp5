/* 31i: ends an overlay_far_on.h region (resident code calls helpers directly). */
#ifdef FROST_OVERLAYS
#undef frost_soft_clip
#undef frost_exp2
#undef frost_log2
#undef frost_sin01
#undef frost_allpass_coef
#undef frost_taper_log
#undef frost_db_to_gain
#undef frost_gain_to_db
#undef frost_half_load
#undef frost_half_store
#endif
