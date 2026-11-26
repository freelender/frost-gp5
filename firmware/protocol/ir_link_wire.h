#ifndef FROST_IR_LINK_WIRE_H
#define FROST_IR_LINK_WIRE_H
/* Stage 4: cabinet IR convolution on the control processor (docs/Build%20Firmware.md).
 * The DSP (SPI master) and the controller (SPI slave + DMA) exchange one frame
 * each way per audio block over SPI 1, framed as OpenGP5 0.5.5's audio link:
 * [sync word][FROST_IRL_WORDS words][FROST_IRL_PAD zero bytes]. The receiver
 * looks for the other side's sync word in the first PAD+1 byte positions (the
 * slave keeps a few bytes queued between exchanges).
 *
 * The DSP starts an exchange right after the last IR block of its chain has
 * written its input, so the controller has about a whole block to convolve.
 * The controller re-arms at once with the buffer it is about to fill and
 * writes the TAG word last: a frame whose tag is not the DSP's block number
 * two blocks back is refused. Latency of the IR path: exactly 2 blocks
 * (128 samples, 2.90 ms) whenever audio flows.
 *
 * Coefficients travel DSP -> controller in a sideband (64 taps per block), so
 * the DSP stays the single source of every IR (built-in, IR SOURCE, CAB file):
 * a stream plays only once the controller reports the DSP's coefficient
 * version as loaded. Both sides include this header. */
#define FROST_IRL_SYNC_D 0xC0DE5A17u   /* DSP -> controller (as OpenGP5) */
#define FROST_IRL_SYNC_N 0xC0DE5A71u   /* controller -> DSP */
#ifdef FROST_USB_AUDIO
#define FROST_IRL_MAGIC_D 0x334C4946u /* FIL3: media frame, refuse a legacy peer */
#define FROST_IRL_MAGIC_N 0x344C4946u /* FIL4 */
#else
#define FROST_IRL_MAGIC_D 0x314C4946u  /* "FIL1" */
#define FROST_IRL_MAGIC_N 0x324C4946u  /* "FIL2" */
#endif
#define FROST_IRL_PAD 32u
#define FROST_IRL_BLOCK 64u
#define FROST_IRL_STREAMS 2u
#define FROST_IRL_TAPS 512u
#define FROST_IRL_CHUNK 64u                                  /* taps per sideband chunk */
#define FROST_IRL_CHUNKS (FROST_IRL_TAPS/FROST_IRL_CHUNK)    /* 8 */
/* Word offsets after the sync word, both directions. */
#define FROST_IRL_W_MAGIC 0u
#define FROST_IRL_W_FLAGS 1u      /* D: bit0 remote mode, bits 8..9 streams written. N: ready versions */
#define FROST_IRL_W_SIDE 2u       /* D: bit31 valid, bits 0..3 stream, 4..7 chunk, 8..15 version. N: ctl cycles */
#define FROST_IRL_W_SCALE 3u      /* D: float bits of the chunk's coefficient scale. N: ctl blocks */
#define FROST_IRL_W_VERSIONS 4u   /* D: current versions v0 | v1 << 8. N: frames refused */
#define FROST_IRL_W_INFO 5u       /* D: DSP cycles. N: USB direct-off in media profile, otherwise 0 */
#define FROST_IRL_W_TAPS 6u       /* D: 32 words = 64 int16 taps (tap 2i low half). N: 0 */
#define FROST_IRL_W_AUDIO 38u     /* 64 int32 per stream: D Q23 input, N raw sums */
#ifdef FROST_USB_AUDIO
/* 64 packed stereo frames each way; the tag remains last. */
#define FROST_IRL_W_USB (FROST_IRL_W_AUDIO+FROST_IRL_STREAMS*FROST_IRL_BLOCK)
#define FROST_IRL_MEDIA_WORDS FROST_IRL_BLOCK
#else
#define FROST_IRL_MEDIA_WORDS 0u
#endif
#define FROST_IRL_W_TAG (FROST_IRL_MEDIA_WORDS+FROST_IRL_W_AUDIO+FROST_IRL_STREAMS*FROST_IRL_BLOCK)  /* 166 legacy / 230 media; written last */
#define FROST_IRL_WORDS (FROST_IRL_W_TAG+1u)                                  /* 167 legacy / 231 media */
#define FROST_IRL_BYTES (4u+FROST_IRL_WORDS*4u+FROST_IRL_PAD)                /* 704 legacy / 960 media */
/* N ready word: version of stream j in bits 8j..8j+7, loaded flag in bit 16+j. */
#define FROST_IRL_READY(v,j) ((((v)>>(16u+(j)))&1u) ? (((v)>>(8u*(j)))&0xffu) : 0x100u)
/* Audio scaling: DSP sends x * 2^23 (|x| <= 16 by the chain contract); the
 * controller accumulates round(x_q * q / 2^16), so y = acc * scale / 128. */
#define FROST_IRL_IN_SCALE 8388608.0f
#define FROST_IRL_OUT_SHIFT 128.0f
/* DSP command (controller -> DSP over the UART link) before a hand-over to the
 * factory loader: stop the SPI exchanges and release SPI 1 (OpenGP5 0x7F05). */
#define FROST_IRL_QUIET_COMMAND 0x7f4eu /* 4D is quick preset recall. */
#define FROST_IRL_STATUS 0u
#define FROST_IRL_LOADER_QUIET 1u
#define FROST_IRL_FLASH_PAUSE 2u
#define FROST_IRL_FLASH_RESUME 3u
#endif
