#ifndef FROST_DSP_USAGE_WIRE_H
#define FROST_DSP_USAGE_WIRE_H
/* Optional FRX1 extension, same 24-byte frame and existing cadence.
 * All existing RX fields are preserved: word +16 bits 30..31 contain the
 * high 2 bits of a 9-bit code; word +20 bits 0..6 contain its low 7 bits.
 * Code = 1 + round(latest completed window's peak block cycles / 2048).
 * Zero means unavailable/stopped/old firmware. Max code saturates at 511.
 * Controller converts to nominal block-budget percent; no DSP division. */
#define FROST_DSP_USAGE_SHIFT 30u
#define FROST_DSP_USAGE_MASK 511u
#define FROST_DSP_USAGE_UNIT 2048u
#define FROST_DSP_USAGE_BUDGET 278494u
#define FROST_DSP_USAGE_REFRESH_MS 2500u
#define FROST_DSP_USAGE_STALE_MS 6000u
#endif
