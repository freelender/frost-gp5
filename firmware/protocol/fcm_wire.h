#ifndef FROST_FCM_WIRE_H
#define FROST_FCM_WIRE_H
#include "slot_wire.h"
/* AMP/FCM model transfer, controller -> DSP (EXPERIMENTAL).
 *
 * Models live as files in the CONTROLLER's flash (OpenGP5 0.5 file format,
 * docs/Build%20Firmware.md), firmware id FROST_FCM_FIRM, named "amp-NN" where NN =
 * the AMP block's MODEL value (00..63). File data is one FCM record:
 * name[16] (ASCII, zero padded), FcmiModel (380 B), crc32 (zlib) over
 * name+model: FROST_FCM_RECORD_BYTES in all.
 *
 * The controller copies the record of the selected MODEL to the DSP with this
 * command; the DSP holds ONE received record (any other MODEL plays the
 * built-in default model). Little-endian words, never native structs.
 *
 * Request [u16 cmd][u16 len][0..]: version, token, op, index, crc, offset,
 * count, then `count` data words (count <= FROST_FCM_CHUNK_WORDS; 0 for every
 * op except WRITE). len = 32 + 4*count.
 * Reply (48 bytes): version, token, status, held index, held crc (0 = none),
 * receive state, receive index, receive crc, receive next word, epoch, 0.
 *
 * STATUS: report only. BEGIN(index,crc): start a record (the held one stays
 * playing in the AMP block's own copy until COMMIT). WRITE(offset,count,data):
 * the next words in order. COMMIT(index,crc): all words in, CRC/structure
 * valid -> held = this record, epoch+1. CLEAR: drop the held record (epoch+1).
 * Repeating the last op is harmless (WRITE of an already received offset is
 * acknowledged, COMMIT of the held record is APPLIED). */
#define FROST_FCM_COMMAND 0x7f48u
#define FROST_FCM_VERSION 1u
#define FROST_FCM_REQUEST_BYTES 32u
#define FROST_FCM_REPLY_BYTES 48u
#define FROST_FCM_CHUNK_WORDS 20u
#define FROST_FCM_RECORD_BYTES 400u
#define FROST_FCM_RECORD_WORDS (FROST_FCM_RECORD_BYTES/4u)
#define FROST_FCM_RECORD_NAME 16u
#define FROST_FCM_MODELS 64u
#define FROST_FCM_FIRM 0x4d434646u            /* "FFCM" little endian */
enum { FROST_FCM_STATUS, FROST_FCM_BEGIN, FROST_FCM_WRITE, FROST_FCM_COMMIT, FROST_FCM_CLEAR };
enum { FROST_FCM_RX_EMPTY, FROST_FCM_RX_RECEIVING, FROST_FCM_RX_HELD };
#endif
