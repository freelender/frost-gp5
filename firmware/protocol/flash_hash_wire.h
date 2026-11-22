#ifndef FROST_FLASH_HASH_WIRE_H
#define FROST_FLASH_HASH_WIRE_H
/* 31e read-only DSP flash hash (diagnostic). Request [cmd][16][token][address][length],
 * reply [cmd][16][token][status][fnv1a32]. Status 0 = hashed; 1 = rejected (range or size).
 * FNV-1a over plain loads of memory-mapped DSP flash. Bounded to the factory-loader
 * sections' DSP addresses and to 1 KB per call, so one call never delays an audio
 * block. No write, erase or flash-controller access exists here. */
#define FROST_FLASH_HASH_COMMAND 0x7f47u
#define FROST_FLASH_HASH_LOW 0x4000u
#define FROST_FLASH_HASH_HIGH 0x157000u
#define FROST_FLASH_HASH_CHUNK 1024u
#endif
