# Frost for Valeton GP-5 — User Guide

Beta: **0.1.0-beta.8** · Website: **1.4.0** · Updated: 23 November 2026

Website: [Frost GP-5 Studio](https://frost.fretlexwins.workers.dev/)

Frost provides a six-slot signal chain, amp heads, converted capture models,
cabinet IRs, editable presets, a tuner, and songs/setlist navigation. Once your
sounds and setlist are saved, the pedal works without the website connected.

## 1. Get started

Install the supplied firmware using the [Installation Guide](Installation%20Guide.MD). Connect your
guitar and normal audio output, starting with a low output volume. USB is used
by the website for pedal control and file transfers; listen through the pedal's
normal audio output and your headphones, interface, amplifier or powered speaker.

For website editing, use desktop Chrome or Edge. Open the website, connect the
powered GP-5 using a USB data cable, and click **Connect GP-5**. Allow MIDI,
including SysEx messages, when the browser asks. Close other pedal editors or
browser tabs using the device.

**Try the demo** lets you explore the live editor without changing a pedal.

## 2. Pedal controls

| Control | Normal use |
|---|---|
| PARA turn | Select a block, menu row, preset or song, depending on the screen |
| PARA short press | Open the selection or enter/leave a value edit |
| PARA hold on a home screen | Open Global settings |
| Footswitch tap on a home screen | Bypass the selected block, advance a preset, or advance a setlist part, depending on the home screen |
| Footswitch tap in a menu | Go back one level (same as **BACK**) |
| Footswitch hold | Open the tuner; Setlist view can instead use Previous Part |

In parameter menus, turn to select a row, press to edit it, turn to change its
value, then press again to leave the value edit. Use **BACK** or tap the
footswitch to return one level. A parameter value is kept as you turned it;
an unconfirmed TYPE, POSITION, CAB or REMOVE choice is cancelled. In Global
settings the footswitch leaves a value edit, and from the settings list it
saves and returns home like **BACK**. Bypass the block from its home screen.
Some effects have their own momentary footswitch behavior on home screens.

### Choose a home screen

Hold PARA on a home screen, open **DISPLAY**, select **HOME SCREEN**, press,
choose a view, then press again. Go back to GLOBAL SETTINGS and choose **BACK**
to save and return.

| Home screen | PARA turn | Footswitch tap |
|---|---|---|
| CHAIN | Select an effect block | Toggle the selected block |
| STOMP | Select an effect; show it prominently | Toggle the selected block |
| PRESET | Load previous/next stored preset | Load next stored preset; wraps at the end |
| SETLIST | Select previous/next song and load its first part | Advance to the next part |

From CHAIN or STOMP, press PARA to open the selected effect's parameters.
From PRESET or SETLIST, press PARA to open the chain for editing. These views
return from the chain to the selected home screen after about 15 seconds idle.

## 3. Build and edit a sound

The signal runs from input through slots 1–6 to output. Empty slots do not
process the sound. One typical electric-guitar chain is:

**Compressor → Overdrive → Amp head → IR → Delay → Reverb**

On the pedal, select a block and press PARA. Use **TYPE** to choose its effect,
**POSITION** to move it in the signal order, and **REMOVE** to clear it after
confirmation. Effect parameter rows control its sound.

On the website, open **Live editor**, select a block, and use its inspector to
change the effect, bypass state and available parameters. Changes affect the
sound immediately, but they are **not saved to a preset** until you save on
the pedal. Selecting another preset or switching off discards unsaved edits.

There are six slots, but processing capacity depends on the effects chosen.
AMP plus five effects is the design target, not a guarantee for every heavy
combination. A second amp or unsupported combination can be refused. If a chain
overloads, simplify it or select a lighter preset.

Effects also share a fixed amount of effect memory on the audio processor.
Most chains fit, but several of the largest effects together (NAM capture,
OCD, POG, an amp head, AW-3, RAT) may not. The pedal then refuses the change
and shows **CHAIN TOO BIG**: the previous chain keeps playing, and a preset
that does not fit is not loaded. Replace one of the larger effects.

The pedal also estimates the processing load of a chain before it plays, from
costs measured for each effect. A chain estimated above about 90% of the DSP
is refused with **DSP LIMIT**, so it does not overload and mute. The heaviest
effects are POG (about 40% on its own), NAM capture (about 31%), the JCM800,
RECTO and 5150 heads (about 28%) and the reverbs (about 18–21%). Both
refusals appear as a popup for about 2.5 seconds. Settings that cost more
than an effect's defaults, such as RV-6 SHIMMER, are not part of this
estimate; overload protection still covers them.

### Read the DSP meter

The DSP percentage on the Chain screen shows a **recent peak**, rather than
an average. It updates every 2.5 seconds. A preset change includes loading,
resetting effect histories and fading between sounds, so a brief switching
spike can remain on the meter for several seconds. Repeated preset changes
can keep that peak visible even when the settled presets run below 100%.

Above 100% means at least one measured audio block exceeded its processing
budget. Let the preset settle before judging its normal load. A brief high
reading alone does not mean the preset stays overloaded, but clicks or short
noise during the change are still switching issues worth reporting. If the
reading stays high while playing one preset, simplify the chain and report
the preset and effects used. Include whether the noise happens only during
switching or continues afterwards.

The 0.1.0-beta.3 source candidate removes duplicate cabinet preparation and
history resets during recall, and handles completion polls in RAM. These
changes retain the same presets, cabinet samples and fades; reduced switching
noise and physical timing still need confirmation on the pedal.

## 4. Amp heads and capture models

The algorithmic amp heads are based on these familiar amp styles:

- Fender '65 Deluxe Reverb
- Fender Twin Reverb
- Vox AC30 Top Boost
- Roland JC-120
- Marshall JCM 800
- Mesa/Boogie Dual Rectifier
- EVH 5150

These are **amp heads only**. Use a separate IR block for the cabinet when
listening through headphones, an audio interface or a full-range speaker.
With a physical guitar cabinet, choose whether to bypass the IR for your setup.

Drive/gain affects distortion; master and level affect the amp's response and
output balance. As a starting point for a newly added Deluxe or AC30 block,
MASTER 60 and LEVEL 57 are the current defaults. High-gain models may need
different levels. Match sounds by listening rather than copying knob values.

The block labelled **NAM** uses Frost FCM capture models. Frost converts
supported NAM A2 captures into approximations; it does not run native NAM.
BASS, MIDDLE and TREBLE adjust the model output by -12 to +12 dB, with
0 dB neutral. They are saved per preset and do not change the capture file.

### Add a capture model

1. Open **AMP models** on the website.
2. Choose a supported `.nam` capture or a ready-made `.fcm` file.
3. For a NAM conversion, optionally choose the recommended training `input.wav`
   described on the page. Click **Convert** and allow a few minutes.
4. Choose a name and MODEL number, then click **Write to pedal** and confirm.
5. Select that model in the NAM block and save the preset on the pedal.

MODEL 00 is built in; MODEL 01–63 are user model positions. Conversion quality
depends on the capture. Replacing a model number changes all presets using it.

## 5. Cabinet IRs

An IR adds the frequency response of a captured cabinet/microphone setup. It
does not replace an amp head. Place IR after the amp for a typical direct tone.

- **CAB 00:** the built-in cabinet. The supplied beta uses Cookie Monster;
  custom source builds may use a different cabinet or the synthetic diagnostic FIR.
- **CAB 01–63:** custom cabinets written from the website.
- **VOL 80:** default for a newly added IR block; adjust by ear for your chain.
- **LOW CUT:** removes low end below the set frequency (12 dB/octave),
  10–400 Hz. **0 HZ** is off (the default).
- **HIGH CUT:** removes treble above the set frequency (12 dB/octave),
  1,000–19,750 Hz. **20000 HZ** is off (the default).

Presets saved before these controls existed load with both cuts off and sound
unchanged. LOW CUT and HIGH CUT act on the cabinet sound before VOL and the
bypass fade.

### Add and select a cabinet

1. Open **Cabinet IRs** and choose a cabinet `.wav` file.
2. Choose a name and CAB number, then **Write to pedal** and confirm.
3. Open the IR block in the live editor or on the pedal and change **CAB** to
   that number. Uploading a file alone does not select it in the active chain.
4. Save the preset on the pedal to retain the CAB selection and IR settings.

The website converts supported WAV files to a mono, 44.1 kHz, 512-tap response.
Long reverberation tails are not retained as a full room impulse response.
Replacing a CAB number changes every preset referencing it. If a referenced
custom cabinet is missing, Frost uses the built-in cabinet until it is replaced.

If IR seems bypassed, check that the block is enabled and Global **CAB OVERRIDE**
is OFF. If the tone is quiet, check amp level, IR VOL, Global OUTPUT LEVEL and
the physical volume control before adding more gain everywhere.

## 6. Save and manage presets

Frost has **60 editable preset slots**. On first use with empty Frost preset
storage, slots 01–20 receive factory sounds and 21–60 remain empty. Factory
sounds use ordinary slots: rename, overwrite or delete them to use all 60 for
your own sounds. Updating firmware preserves an existing Frost bank.

### Save the current sound

1. Finish editing on the pedal or website.
2. On the pedal, open a block's parameters and select **PRESETS**.
3. Choose the destination preset number and press PARA.
4. Select **SAVE**, then press PARA. Saving to an occupied slot replaces it.

To load through this menu, choose the preset number, press PARA, select **LOAD**
and press again. PRESET home offers quicker footswitch/PARA navigation.

The website's **Presets** page can rename, reorder and delete stored presets.
Live-editor changes still need the on-pedal SAVE operation.

The boot preset is remembered on explicit preset SAVE or when leaving Global
settings; simply browsing presets does not continuously save a new boot choice.

### Factory sounds

| Slot | Preset | Slot | Preset |
|---|---|---|---|
| 01 | Acoustic | 11 | Classic Crunch |
| 02 | Deluxe Clean | 12 | Arena Rock |
| 03 | Twin Sparkle | 13 | Brown Crunch |
| 04 | Jazz Club | 14 | Modern Rhythm |
| 05 | Nashville Snap | 15 | Tight Metal |
| 06 | Brit Jangle | 16 | Singing Lead |
| 07 | Edge of Chime | 17 | Fuzz Sunset |
| 08 | Blues Breakup | 18 | Liquid Vibe |
| 09 | Texas Drive | 19 | Funk Filter |
| 10 | Vintage Trem | 20 | Ambient Bloom |

Acoustic uses compression, acoustic simulation and reverb, without an amp head
or electric cabinet. Factory reverb mix/effect level stays at or below 25%.
Levels were balanced using an offline reference; your guitar and playing can
still produce differences.

## 7. Songs and setlists

A song contains named parts such as Intro, Verse and Chorus. Each part refers
to an existing preset number. A setlist arranges songs in performance order.

1. Save the presets you want to use first.
2. Open **Songs & setlist** on the website. If preserving an existing pedal
   setlist, use **Read from pedal** before editing your draft.
3. Create a song, name its parts, and assign a preset number to each part.
4. **Add the song to the setlist order.** A song in the library alone will not
   appear in the pedal's performance setlist.
5. Name the setlist, arrange the songs, and choose **Save to pedal**. Confirm
   and wait for the transfer and display refresh to finish.
6. On the pedal, choose Global **HOME SCREEN → SETLIST**, then **BACK**.

The display shows the setlist, song position, song, part and referenced preset.
Tap the footswitch to advance a part. From a song's last part, the next tap
loads the next song's first part. It stops at the end of the setlist.

Global **SETLIST HOLD** chooses **PREV PART** (default) or **TUNER**. Previous
part crosses song boundaries and stops at the beginning. Turning PARA selects
songs. Rebooting in Setlist view starts at the first part of the first song.

There is one active setlist on the pedal. Use JSON export/import on the website
to keep alternatives on your computer. Browser drafts are separate from pedal
storage and are not transferred until you save.

The limits are up to 64 library songs, 16 parts per song and 64 performance
entries, subject to shared storage. For example, 44 songs with four parts or
32 songs with six parts fit. The website checks the storage limit.

Parts reference slot numbers, not copies of presets. Moving, deleting or
replacing a preset changes what referencing parts play. Check your setlist
after reorganizing presets. Preset changes reset effect histories; delay and
reverb tails do not spill over. Avoid editing/writing storage while performing,
as saving can briefly interrupt audio.

## 8. Global settings and tuner

Hold PARA on a home screen to open **GLOBAL SETTINGS**. It lists three
menus: **AUDIO**, **DISPLAY** and **SYSTEM**. Turn to select a row and press to
open a menu or edit a setting; turn to change the value and press again.
**BACK** (or the footswitch) goes up one level; BACK in GLOBAL SETTINGS saves
and exits.

| Menu | Setting | Purpose |
|---|---|---|
| AUDIO | INPUT GAIN | Adjust input level from -20 to +20 dB |
| AUDIO | OUTPUT LEVEL | Scale overall output, 0–100% |
| AUDIO | OUTPUT MODE | Mono/stereo preference; current effect processing is mono |
| AUDIO | CAB OVERRIDE | ON bypasses IR blocks globally without changing presets |
| AUDIO → GLOBAL EQ | GLOBAL BASS / MIDDLE / TREBLE | Three-band output EQ, -12 to +12 dB; 0 dB is flat |
| DISPLAY | HOME SCREEN | Choose CHAIN, PRESET, STOMP or SETLIST |
| DISPLAY | BRIGHTNESS | Adjust screen brightness |
| DISPLAY | SETLIST HOLD | Choose previous part or tuner for a Setlist footswitch hold |
| SYSTEM | FIRMWARE | Release version; this beta displays 0.1.0-BETA.8 |
| SYSTEM | RESET SETTINGS | Reset global preferences while retaining stored sounds and songs |
| SYSTEM | FACTORY RESET | Erase user presets, models, cabinets, songs and settings after confirmation |

Global EQ follows the complete effect chain, consumes no effect slot and stays
the same when switching presets. It also remains active during MIDI global
bypass, as an output correction. Flat global EQ skips its audio processing.
Leave GLOBAL SETTINGS with BACK to save it with the other settings.

To use the tuner, hold the footswitch on a home screen where the tuner gesture
is available. In Setlist view, choose TUNER for SETLIST HOLD first. Tap the
footswitch to exit. In the tuner, press PARA to unlock reference adjustment,
turn to choose 430–450 Hz, and press to lock again; the usual reference is 440 Hz.

### MIDI performance control

USB MIDI controllers can recall presets, switch blocks, control parameters
and expression, tap tempo, open the tuner, change scene masks, bypass globally
and supply MIDI Clock. Use channel 1 and the mappings in
[MIDI commands](Midi%20Command.md). A Chocolate Plus uses its USB host port;
controllers without a host need a computer or compatible MIDI host/router.
These new functions require physical testing with your controller setup.

The website's **Presets → Load** button also recalls a saved sound without
touching the pedal. Save current live edits first if you want to keep them.

## 9. Backups

Connect the pedal, open **Presets**, and choose **Back up everything**. Keep the
downloaded `.frostbackup` file. Current backups include stored presets, models,
IRs, global settings and songs/setlist; they do not include unsaved live edits.

**Restore backup** replaces pedal data with the backup's contents after
confirmation. Back up the current contents first. Restart after restoring
global preferences. A Frost backup is separate from Valeton stock data and is
not a firmware image.

## 10. Troubleshooting and beta feedback

| Symptom | Check |
|---|---|
| Cannot connect | Desktop Chrome/Edge, USB data cable, MIDI permission, other editors/tabs closed; reconnect and reload |
| Old page or unexpected website error | Hard reload with Ctrl+Shift+R |
| Edits disappear | Save the chain with on-pedal PRESETS → destination → SAVE |
| Uploaded IR has no effect | Select its CAB number, enable IR, and turn CAB OVERRIDE OFF |
| NO SETLIST | Add library songs to performance order and Save to pedal |
| Part will not load | Check that its referenced preset exists |
| Quiet or distorted sound | Check input/output gain, amp/IR levels, physical volume and downstream input level |
| Overload or repeated cuts | Select a lighter preset; report the exact chain and reproduction steps |
| CHAIN TOO BIG | The chosen effects do not fit together; replace one of the larger effects (see section 3) |
| DSP LIMIT | The chain would overload the audio processor; replace a heavy effect (see section 3) |

This is beta firmware. Not every hardware setup and heavy chain has been
qualified. Report firmware version, OS/browser, guitar/pickups, audio
routing, preset/chain/CAB, exact steps and whether a reboot changes the problem.
Include a recording or backup when useful, and send it directly to the person
who supplied your beta firmware.

## 11. Effect reference

The following table lists every selectable effect/amp type in this version.
These are Frost implementations inspired by familiar sound styles, not claims
of identical or manufacturer-endorsed pedal/amp emulation. An effect's menu
shows its available controls and units. Start with the defaults and change
one control at a time.

### Common controls

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

### Available effects

#### Overdrive

| Label | Sound / function | Controls |
|---|---|---|
| DRIVE | Overdrive | GAIN, TONE, VOL |
| TS | Ts808/Ts9 Style Od | DRIVE, TONE, LEVEL, MODEL |
| BD-2 | Bd-2 Style Blues Od | GAIN, TONE, LEVEL |
| KLON | Klon Style Od | GAIN, TREBLE, OUTPUT |
| BLUESB | Bluesbreaker Style | GAIN, TONE, VOLUME |
| OCD | Ocd Style Drive | DRIVE, TONE, VOLUME, PEAK |
| TREBLE | Rangemaster Style Boost | BOOST, RANGE, LEVEL |

#### Distortion

| Label | Sound / function | Controls |
|---|---|---|
| DS-1 | Ds-1 Style Dist | DIST, TONE, LEVEL |
| RAT | Rat Style Dist | DIST, FILTER, VOLUME |
| DIST+ | Dist+ Style | DIST, OUTPUT |
| FUZZ | Vintage Fuzz Face Style | FUZZ, VOL, BIAS |

The overdrive, distortion and fuzz models use anti-aliased clipping: high
notes at high gain stay clean of the inharmonic "fizz" that plain digital
clipping adds. The clipped sound is a touch softer in the top octave
(above about 8 kHz), which their own tone stages mostly roll off anyway.

#### Delay

| Label | Sound / function | Controls |
|---|---|---|
| DELAY | Mono Delay | TIME, REPEAT, MIX |
| DD-3 | Dd-3 Style Digital | E.LEVEL, F.BACK, TIME, HOLD, FOOT |
| DM-2 | Dm-2 Style Analog | RATE, ECHO, INTENS |
| TAPE | Tape Echo | TIME, REPEAT, MIX, WOW, AGE, DRIVE |

#### Reverb

Reverb excitation uses a fixed two-pole 180 Hz bass cut on the wet path.
REVERB and RV-6 also shorten low-frequency feedback decay below 300 Hz;
TONE continues to control brightness. Dry guitar is unaffected by these
filters. New RV-6 blocks start at E.LEVEL 25 and TIME 30; new SPRING blocks
start at MIX 20. Existing presets retain their stored controls, but their
reverb sound changes with the updated DSP. SPRING has a shorter nominal
1.8-second tank decay; DWELL controls tank drive, not decay time.

RV-6 SHIMMER is a long hall (about 2 to 10 seconds with TIME) whose tail
blooms with octave-up overtones: an octave-up copy of the tail is fed back
into the reverb, so the sparkle builds and climbs as the sound sustains.
TIME also raises how much octave is fed back; TONE sets the brightness of
both. In firmware built with the compact reverb (`--reverb-int16`, the
current line-test build) the octave is spliced in phase with the sound and
stays in tune from low E upward, and SHIMMER costs about as much DSP as
HALL plus a third. Builds without that option use the older, heavier
shimmer, which can sound up to about a semitone out of tune.


| Label | Sound / function | Controls |
|---|---|---|
| REVERB | Mono Reverb | DECAY, TONE, MIX |
| RV-6 | Rv-6 Style Reverb | E.LEVEL, TONE, TIME, MODE |
| SPRING | 6G15 Style Spring | DWELL, MIX, TONE |

#### Modulation

| Label | Sound / function | Controls |
|---|---|---|
| CHORUS | Mono Chorus | RATE, DEPTH, MIX |
| FLANGE | Mono Flanger | RATE, DEPTH, RESO |
| PH-90 | Phase 90 Style | SPEED, VOICE |
| CE-2 | Ce-2 Style Chorus | RATE, DEPTH |
| MISTRS | Mistress Style Flange | RATE, RANGE, COLOR, MATRIX |
| M117 | M117 Style Flanger | MANUAL, WIDTH, SPEED, REGEN |
| VIBE | Uni-Vibe Style | SPEED, INTENS, MODE, VOLUME |
| VB-2 | Vb-2 Style Vibrato | RATE, DEPTH, RISE, MODE |
| TR-2 | Tr-2 Style Tremolo | RATE, WAVE, DEPTH |
| AW-3 | Aw-3 Style Dynamic Wah | SENS, MANUAL, DECAY, MODE, VOWEL 1, VOWEL 2, RATE, MIX |
| FOXX | Foxx Style Foot Phaser | SPEED, DEPTH, REGEN, STAGES, MANUAL, MODE, MIX |

#### Dynamics / Eq

| Label | Sound / function | Controls |
|---|---|---|
| DYNA | M102 Style Dyna Comp | OUTPUT, SENS |
| COMP+ | Comp Plus Style | SUSTAIN, LEVEL, BLEND, TONE, PICKUP |
| GE-7 | Ge-7 Style Graphic Eq | 100, 200, 400, 800, 1.6K, 3.2K, 6.4K, LEVEL |
| NS-2 | Ns-2 Style Suppressor | THRESH, DECAY, MODE |
| NS-1X | Ns-1X Style Suppressor | THRESH, DECAY, DAMP, MODE |
| MB-CMP | Three Band Compressor | LOW THR, MID THR, HI THR, RATIO, ATTACK, RELEASE, MIX, LEVEL |

#### Utility / Cab

| Label | Sound / function | Controls |
|---|---|---|
| GAIN | Input Gain | GAIN |
| GATE | Noise Gate | THRESH |
| EQ | Three Band Eq | BASS, MID, TREBLE |
| IR | Cabinet Ir | VOL, LOW CUT, HIGH CUT |
| HPF | High Pass Filter | FREQ, Q, SLOPE |
| LPF | Low Pass Filter | FREQ, Q, SLOPE |
| ACOUST | Acoustic Simulator | BODY, TOP, MODE, MIX, LEVEL |

#### Amp / Capture

| Label | Sound / function | Controls |
|---|---|---|
| NAM | Nam Fcm Capture | MODEL, GAIN, LEVEL, BASS, MIDDLE, TREBLE |
| DELUXE | Fender 65 Deluxe Head | GAIN, BASS, MID, TREBLE, BRIGHT, MASTER, LEVEL |
| TWIN | Fender Twin Reverb Head | GAIN, BASS, MID, TREBLE, BRIGHT, MASTER, LEVEL |
| AC30 | Vox Ac30 Top Boost Head | GAIN, BASS, MID, TREBLE, CUT, MASTER, LEVEL |
| JC-120 | Roland Jc-120 Head | GAIN, BASS, MID, TREBLE, BRIGHT, MASTER, LEVEL |
| JCM800 | Marshall Jcm800 Head | GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, LEVEL |
| RECTO | Mesa Dual Rectifier Head | GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, LEVEL |
| 5150 | Evh 5150 Head | GAIN, BASS, MID, TREBLE, PRESENCE, MASTER, LEVEL |

#### Pitch

| Label | Sound / function | Controls |
|---|---|---|
| POG | Pog Style Octave Exp | DRY, SUB OCT, OCT UP |
| DROP | Drop Tune | DROP, MIX |

### Effect-specific tips

- **DD-3:** FOOT=BYPASS uses the normal selected-block bypass. FOOT=HOLD
  makes the selected footswitch hold/release the delay's hold function.
- **VB-2:** MODE=UNLATCH is momentary: the selected effect stays enabled
  while the footswitch is held. Other modes use their normal behavior.
- **ACOUST:** choose STANDARD, JUMBO, PIEZO or ENHANCE, then adjust BODY,
  TOP and MIX. For a starting acoustic-like sound, bypass electric amp/cab
  blocks and add only a little compression and reverb.
- **DROP:** choose the tuning shift and mix. Pitch processing can add latency
  and artifacts; check it with your own chords and playing before performance.
- **POG:** the octave model is experimental. Treat sound quality and tracking
  as beta behavior rather than an exact reproduction of a commercial pedal.
- **Gates/suppressors:** start with a low threshold and raise it until idle
  noise is controlled, then verify soft notes and sustained notes are retained.
- **IR and amp blocks:** an amp head and a cabinet are separate. Match output
  levels with the cabinet active for direct/headphone listening.

## 12. A simple first session

1. Load Deluxe Clean at low listening volume.
2. Choose Chain view, turn PARA to IR, and tap the footswitch to compare
   cabinet on/off. Only that selected block is toggled.
3. Press PARA on an effect, adjust one value and use BACK to return.
4. Save to an unused preset using PRESETS → destination → SAVE.
5. Choose Preset home and use PARA/footswitch to change saved sounds.
6. Hold the footswitch to tune, then tap it to return.
7. Back up the saved bank from the website before making larger changes.

More detail: [Website Guide](Website%20Guide.md) ·
[Installation Guide](Installation%20Guide.MD).
