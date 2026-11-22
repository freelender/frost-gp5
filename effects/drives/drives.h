#ifndef FROST_DRIVES_H
#define FROST_DRIVES_H
/* Pedal-inspired overdrive/distortion models (TS808/TS9, BD-2, Klon,
 * Bluesbreaker, OCD, DS-1, RAT, Distortion+). Each kernel implements its own
 * topology: filter placement, clipping element/location, tone network and
 * gain law differ per model (see README.md). Per-instance state only; no
 * workspace. Parameters use the library's human units (knobs 0..100). */
#define FROST_DRIVE_STATE 12u
typedef struct { float z[FROST_DRIVE_STATE]; } FrostDrive;
void frost_drive_reset(FrostDrive *s);
float frost_ts_sample(FrostDrive *s,float x,const float *p);
float frost_bd2_sample(FrostDrive *s,float x,const float *p);
float frost_klon_sample(FrostDrive *s,float x,const float *p);
float frost_bluesbreaker_sample(FrostDrive *s,float x,const float *p);
float frost_ocd_sample(FrostDrive *s,float x,const float *p);
float frost_ds1_sample(FrostDrive *s,float x,const float *p);
float frost_rat_sample(FrostDrive *s,float x,const float *p);
float frost_distplus_sample(FrostDrive *s,float x,const float *p);
#endif
