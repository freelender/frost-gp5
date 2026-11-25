<p align="center"><img src="../assets/branding/frost_logo.webp" alt="GP-5 Frost" width="340"></p>

# Frost Effects List

Every effect and amp in Frost, with its label on the pedal, its controls and
the pedal or amp that inspired its sound.

[Overdrive](#overdrive) · [Distortion](#distortion) · [Delay](#delay) ·
[Reverb](#reverb) · [Modulation](#modulation) · [Dynamics / EQ](#dynamics--eq) ·
[Utility / Cab](#utility--cab) · [Amp / Capture](#amp--capture) · [Pitch](#pitch) ·
[Common controls](#common-controls) · [Tips](#effect-specific-tips)

These are Frost's own DSP models, not exact or manufacturer-endorsed
reproductions. Select an effect on the pedal or the website to see its controls
and units; start from the defaults and change one control at a time.

All product and company names mentioned in this list (for example Boss,
Roland, Ibanez, Electro-Harmonix, MXR, Dunlop, Fender, Marshall, Vox, Mesa/Boogie,
EVH, Fulltone, Keeley, Klon, ProCo, Dallas, Shin-ei, Valeton and GP-5) are
trademarks of their respective owners. They are used only to describe the
sound a Frost model was inspired by. Frost is an independent project and is
not affiliated with, endorsed or sponsored by any of them. Frost's models are
its own DSP and are not exact reproductions.

## Overdrive

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| DRIVE | Overdrive | GAIN, TONE, VOL | — |
| SCREAM | Screaming Mid OD | DRIVE, TONE, LEVEL, MODEL | Ibanez Tube Screamer TS808/TS9 |
| BLUESY | Bluesy Crunch OD | GAIN, TONE, LEVEL | Boss BD-2 Blues Driver |
| HORSE | Golden Horse OD | GAIN, TREBLE, OUTPUT | Klon Centaur |
| BREAK | British Breakup OD | GAIN, TONE, VOLUME | Marshall Bluesbreaker pedal |
| OBSESS | Obsessive Drive | DRIVE, TONE, VOLUME, PEAK | Fulltone OCD |
| RANGER | Treble Ranger Boost | BOOST, RANGE, LEVEL | Dallas Rangemaster |
| BOOST | Clean Boost: 0 to +20 dB, BRIGHT adds a treble lift | GAIN, BRIGHT | — |

## Distortion

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| ORANGE | Orange Box Dist | DIST, TONE, LEVEL | Boss DS-1 |
| MOUSE | Mouse Distortion | DIST, FILTER, VOLUME | ProCo RAT |
| PLUS | Plus Distortion | DIST, OUTPUT | MXR Distortion+ |
| FACE | Round Face Fuzz | FUZZ, VOL, BIAS | Dallas Arbiter Fuzz Face |
| BIG PI | Big Pi Fuzz: two clipping stages; TONE in the middle scoops the mids | SUSTAIN, TONE, VOLUME | Electro-Harmonix Big Muff Pi |
| CRUSH | Bit Crusher: fewer BITS and a lower RATE (kHz) sound harsher | BITS, RATE, MIX | — |

The overdrive, distortion and fuzz models use anti-aliased clipping, so high
notes at high gain stay free of the harsh "fizz" that plain digital clipping
adds. The very top octave (above about 8 kHz) is a touch softer.

## Delay

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| DELAY | Mono Delay | TIME, REPEAT, MIX | — |
| DIGI | Digital Delay | E.LEVEL, F.BACK, TIME, HOLD, FOOT, TAIL | Boss DD-3 |
| BBD | Bucket Brigade Delay | RATE, ECHO, INTENS, TAIL | Boss DM-2 |
| TAPE | Tape Echo | TIME, REPEAT, MIX, WOW, AGE, DRIVE, TAIL | — |
| PING | Ping Pong Delay: stereo, echoes alternate left and right (WIDTH sets the spread); plays as a mono delay with OUTPUT MODE MONO or when an amp, drive, cab, pitch or dynamics block follows it | TIME, F.BACK, E.LEVEL, WIDTH, TAIL | — |

**TAIL** (DIGI, BBD, TAPE, SPACE, SPRING; OFF by default): with TAIL ON the
echoes or reverb ring out when you switch the effect off; with TAIL OFF it
stops at once. See [Delay and reverb tails](User%20Guide.md#delay-and-reverb-tails).

## Reverb

The reverbs cut the lowest bass from the reverb sound only (your dry guitar is
untouched), so the tail stays clear. SPRING has a short tank (about 1.8 s);
DWELL drives the tank rather than lengthening it.

SPACE has five MODEs: ROOM, HALL, PLATE, MOD and SHIMMER. SHIMMER is a long hall
(about 2 to 10 seconds with TIME) whose tail blooms with octave-up overtones
that build as the sound sustains; TIME also sets how much octave is fed back
and TONE the brightness. The modes cost different amounts of DSP (ROOM and HALL
least, SHIMMER most), so a heavy chain may refuse a costlier mode with
DSP LIMIT.

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| REVERB | Mono Reverb | DECAY, TONE, MIX | — |
| SPACE | Space Reverb 5 Modes | E.LEVEL, TONE, TIME, MODE, TAIL | Boss RV-6 |
| SPRING | Tube Spring Reverb | DWELL, MIX, TONE, TAIL | Fender 6G15 reverb unit |

## Modulation

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| CHORUS | Mono Chorus | RATE, DEPTH, MIX | — |
| FLANGE | Mono Flanger | RATE, DEPTH, RESO | — |
| PHASE | Script Phaser | SPEED, VOICE | MXR Phase 90 |
| WARM | Warm BBD Chorus | RATE, DEPTH | Boss CE-2 |
| LADY | Electric Lady Flange | RATE, RANGE, COLOR, MATRIX | Electro-Harmonix Electric Mistress |
| JET | Jet Flanger | MANUAL, WIDTH, SPEED, REGEN | MXR M117 Flanger |
| VIBE | Photocell Vibe | SPEED, INTENS, MODE, VOLUME | Shin-ei Uni-Vibe |
| WARBLE | Warble Vibrato | RATE, DEPTH, RISE, MODE | Boss VB-2 |
| TREM | Classic Tremolo | RATE, WAVE, DEPTH | Boss TR-2 |
| H-TREM | Harmonic Tremolo: lows and highs pulse in opposite phase | RATE, DEPTH, LEVEL | — |
| ROTARY | Rotary Speaker: SLOW/FAST rotors glide between speeds; BALANCE mixes drum and horn | SPEED, DEPTH, BALANCE, LEVEL | — |
| RING | Ring Modulator: FREQ is the carrier in Hz | FREQ, MIX | — |
| ENVWAH | Envelope Auto Wah | SENS, MANUAL, DECAY, MODE, VOWEL 1, VOWEL 2, RATE, MIX | Boss AW-3 |
| FOXY | Foxy Foot Phaser | SPEED, DEPTH, REGEN, STAGES, MANUAL, MODE, MIX | Foxx foot phaser |

## Dynamics / EQ

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| SQUASH | Squash Comp | OUTPUT, SENS | MXR M102 Dyna Comp |
| BLEND | Blend Comp | SUSTAIN, LEVEL, BLEND, TONE, PICKUP | Keeley Compressor Plus |
| EQ-7 | 7-Band Graphic EQ | 100, 200, 400, 800, 1.6K, 3.2K, 6.4K, LEVEL | Boss GE-7 |
| CALIEQ | Cali 5-Band EQ: broad bands for the classic "V" (80 and 6.6K up, 750 down); best after an amp | 80, 240, 750, 2.2K, 6.6K, LEVEL | Mesa/Boogie Mark IIC+ graphic EQ |
| NOISE | Noise Suppressor | THRESH, DECAY, MODE | Boss NS-2 |
| NOISEX | Adaptive Suppressor | THRESH, DECAY, DAMP, MODE | Boss NS-1X |
| MB-CMP | Three Band Compressor | LOW THR, MID THR, HI THR, RATIO, ATTACK, RELEASE, MIX, LEVEL | — |
| SWELL | Volume Swell: each note after a pause fades in over ATTACK ms; higher SENS swells quieter notes | ATTACK, SENS, LEVEL | — |

## Utility / Cab

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| GAIN | Input Gain | GAIN | — |
| GATE | Noise Gate | THRESH | — |
| EQ | Three Band Eq | BASS, MID, TREBLE | — |
| IR | Cabinet Ir | VOL, LOW CUT, HIGH CUT | — |
| HPF | High Pass Filter | FREQ, Q, SLOPE | — |
| LPF | Low Pass Filter | FREQ, Q, SLOPE | — |
| ACOUST | Acoustic Simulator | BODY, TOP, MODE, MIX, LEVEL | Boss AC-3 |

## Amp / Capture

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| NAM | Nam Fcm Capture | MODEL, GAIN, LEVEL, BASS, MIDDLE, TREBLE | — |
| US-DLX | US Deluxe Clean | GAIN, BASS, MID, TREBLE, BRIGHT, MASTER, LEVEL | Fender '65 Deluxe Reverb |
| US-DBL | US Double Clean | GAIN, BASS, MID, TREBLE, BRIGHT, MASTER, LEVEL | Fender Twin Reverb |
| CHIME | Brit Chime 30 | GAIN, BASS, MID, TREBLE, CUT, MASTER, LEVEL | Vox AC30 Top Boost |
| JAZZ | Jazz Clean 120 | GAIN, BASS, MID, TREBLE, BRIGHT, MASTER, LEVEL | Roland JC-120 |
| UK-800 | Brit 800 Crunch | GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, LEVEL | Marshall JCM800 |
| CALI | Cali High Gain | GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, LEVEL | Mesa/Boogie Dual Rectifier |
| BROWN | Brown Sound High Gain | GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, LEVEL | EVH 5150 |
| SL-100 | Super Lead 100: three channels (MODE: CLEAN, CLEAN BRT, CRUNCH, CRUNCH BRT, LEAD; BRT = bright cap) | MODE, GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, LEVEL | Soldano SLO-100 |

The amp heads are amps only: add an IR block after them for the cabinet. NAM
plays a converted capture (FCM) chosen by MODEL; captures are added on the
website. With single-coil pickups, see [Single coils and humbuckers](User%20Guide.md#single-coils-and-humbuckers).

## Pitch

| Label | Sound / function | Controls | Inspired by |
|---|---|---|---|
| OCTAVE | Polyphonic Octaver | DRY, SUB OCT, OCT UP | Electro-Harmonix POG |
| DROP | Drop Tune | DROP, MIX | — |

## Common controls

| Control | How to use it |
|---|---|
| GAIN / DRIVE | Increase input drive/distortion; recheck output loudness afterwards |
| VOL / LEVEL / MASTER | Balance loudness; amp MASTER can also change the response |
| MIX / E.LEVEL | Blend the effect or set its wet contribution |
| TONE / TOP / BASS / TREBLE | Adjust the tonal balance |
| TIME | Space between repeats; milliseconds where marked MS |
| F.BACK / FEEDBACK | Number/strength of repeats; start low |
| RATE / SPEED | Modulation speed; frequency where marked HZ |
| DEPTH | Amount of modulation |
| THRESH / SENS | Gate/filter sensitivity; raise carefully so quiet playing remains audible |

## Effect-specific tips

- **DIGI:** FOOT=BYPASS uses the normal selected-block bypass. FOOT=HOLD
  makes the selected footswitch hold/release the delay's hold function.
- **WARBLE:** MODE=UNLATCH is momentary: the selected effect stays enabled
  while the footswitch is held. Other modes use their normal behavior.
- **ACOUST:** choose STANDARD, JUMBO, PIEZO or ENHANCE, then adjust BODY,
  TOP and MIX. For a starting acoustic-like sound, bypass electric amp/cab
  blocks and add only a little compression and reverb.
- **DROP:** choose the tuning shift and mix. Each pick attack plays about
  1.5 ms after you hit the string; held notes then lag by up to about one
  period of the lowest note (up to about 25 ms for a low chord). Very dense or
  out-of-tune chords can still waver slightly; check it with your own playing
  before performance.
- **OCTAVE:** the octave model is experimental. Single notes and simple chords
  track cleanly; wide six-string chords, especially an octave up, still sound
  smeared. Treat it as beta behavior rather than an exact reproduction of a
  commercial pedal.
- **Gates/suppressors:** start with a low threshold and raise it until idle
  noise is controlled, then verify soft notes and sustained notes are retained.
- **IR and amp blocks:** an amp head and a cabinet are separate. Match output
  levels with the cabinet active for direct/headphone listening.

More: [User Guide](User%20Guide.md) · [Website Guide](Website%20Guide.md) ·
[MIDI commands](Midi%20Command.md)
