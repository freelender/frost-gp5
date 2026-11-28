<p align="center"><img src="../assets/branding/frost_logo.webp" alt="GP-5 Frost" width="340"></p>

# MIDI commands — setup and examples

Set your controller to **MIDI channel 1**.

The release firmware also accepts these same
messages from a BLE MIDI client. It advertises as **Frost**. BLE MIDI is
wireless control, not Bluetooth audio; firmware installation remains USB-only.
See [Build Firmware](Build%20Firmware.md) for the profile and qualification status.
You can use these instructions in a controller editor without typing raw MIDI bytes.

Start here: [COMP+ LEVEL example](#example-set-comp-level-to-10) ·
[Switch effects on/off](#switch-an-effect-on-or-off) ·
[Find a parameter CC](#find-the-cc-number-for-a-control) ·
[Choose the MIDI value](#choose-the-midi-value) ·
[All effect controls](#effect-control-lookup) ·
[Connect your controller](#connect-a-controller).

## The three settings you enter

For most actions, choose **CC / Control Change** in your controller editor,
then fill in these fields:

| Controller field | What it means | Example |
|---|---|---|
| Channel | Which MIDI channel Frost listens to | **1** |
| CC number / Controller number | Which switch or effect control to change | **33** = block 1, second control |
| Value / CC value | Where to set that control, from 0 to 127 | **13** = LEVEL 10 for COMP+ |

**CC number chooses the control. MIDI value sets that control.**
The MIDI value is often different from the number shown on the pedal.
For example, LEVEL 10 needs MIDI value 13, not MIDI value 10.
All numbers in the setup tables are **decimal**, as used by most controller editors.

### Example: set COMP+ LEVEL to 10

This example assumes **COMP+ is the first block in your chain**, reading
left to right on the website or following the signal arrows on the pedal.

1. Choose a controller button and set its message type to **CC / Control Change**.
2. Set **Channel = 1**.
3. Set **CC number = 33**.
4. Set **Value = 13**.
5. Send the message. COMP+ **LEVEL should show 10** on the pedal/website.
   If you want to keep the edit, save the preset on the pedal.

Copy these settings:

| Message type | Channel | CC number | MIDI value | Pedal result |
|---|---|---|---|---|
| **CC** | **1** | **33** | **13** | **COMP+ LEVEL = 10**, when COMP+ is in block 1 |

COMP+ controls are **1 SUSTAIN, 2 LEVEL, 3 BLEND, 4 TONE, 5 PICKUP**.
LEVEL is its second control. The CC number changes if COMP+ moves:

| COMP+ position in chain | LEVEL CC number | MIDI value for LEVEL 10 |
|---|---|---|
| Block 1 | **33** | **13** |
| Block 2 | **41** | **13** |
| Block 3 | **49** | **13** |
| Block 4 | **57** | **13** |
| Block 5 | **65** | **13** |
| Block 6 | **73** | **13** |

There is no permanent “COMP+ CC number.” MIDI addresses the **block position**
and the **control within that block**. A different preset can put a different
sound in the same position, so check the chain for each preset.

## Switch an effect on or off

Use **CC**, channel **1**. These commands switch the whole block; they do not
change LEVEL, MIX or any other control.

| Block position | CC number | MIDI value to turn OFF | MIDI value to turn ON |
|---|---|---|---|
| 1 | **20** | **0** | **127** |
| 2 | **21** | **0** | **127** |
| 3 | **22** | **0** | **127** |
| 4 | **23** | **0** | **127** |
| 5 | **24** | **0** | **127** |
| 6 | **25** | **0** | **127** |

Example: COMP+ in block 1 → **CC 20, value 127** turns it on.
**CC 20, value 0** turns it off. **CC 33, value 13** changes its LEVEL to 10.
These are separate actions.

For one button that alternates on/off, select your controller's **toggle**
mode and configure alternating values **127 and 0**. Sending 127 repeatedly
keeps the effect on; Frost does not invert the state with each message.
Values 0–63 mean off and 64–127 mean on, but using 0/127 is simplest.
Empty blocks cannot be enabled.

## Find the CC number for a control

1. Find the effect's **block position**, 1–6.
2. Find the control's **number in that effect**, using the
   [effect control lookup](#effect-control-lookup) below. The website's inspector
   also shows the controls in this order.
3. Look across the block's row to that control's column. That is the **CC number**.
4. Choose the **MIDI value** using the next section.

| Block position | Control 1 | Control 2 | Control 3 | Control 4 | Control 5 | Control 6 | Control 7 | Control 8 |
|---|---|---|---|---|---|---|---|---|
| 1 | 32 | 33 | 34 | 35 | 36 | 37 | 38 | 39 |
| 2 | 40 | 41 | 42 | 43 | 44 | 45 | 46 | 47 |
| 3 | 48 | 49 | 50 | 51 | 52 | 53 | 54 | 55 |
| 4 | 56 | 57 | 58 | 59 | 60 | 61 | 62 | 63 |
| 5 | 64 | 65 | 66 | 67 | 68 | 69 | 70 | 71 |
| 6 | 72 | 73 | 74 | 75 | 76 | 77 | 78 | 79 |

Example: DELAY is in **block 5** and MIX is **control 3**.
Row 5, control 3 gives **CC 66**. To set MIX to 25, send **value 32**.

Moving a block changes its CC assignments. The extra IR **CAB file selector**
is not one of these controls; IR's only parameter here is VOL. TYPE, POSITION,
REMOVE, PRESETS and other menu actions are not effect parameters.

## Choose the MIDI value

### Controls with a 0–100 range

Use this table for controls such as COMP+ LEVEL, delay MIX and IR VOL **when
that control's displayed range is 0–100**. It is not a conversion table for
milliseconds, Hz, dB ranges or named choices.

| Desired number on the pedal | MIDI value to enter |
|---|---|
| 0 | **0** |
| 5 | **6** |
| 10 | **13** |
| 20 | **25** |
| 25 | **32** |
| 30 | **38** |
| 40 | **51** |
| 50 | **64** |
| 60 | **76** |
| 70 | **89** |
| 75 | **95** |
| 80 | **102** |
| 90 | **114** |
| 100 | **127** |

For other 0–100 values, multiply the desired number by **127 ÷ 100** and round
to the nearest whole number. Example: LEVEL 10 → 10 × 127 ÷ 100 = 12.7 → **13**.
This table's results assume normal one-unit steps.

### Named choices, such as MODE or PICKUP

Use the order shown in the effect lookup. Pick one MIDI value from the row
matching the number of choices:

| Number of choices | MIDI values, in choice order |
|---|---|
| 2 | **0**, **127** |
| 3 | **0**, **64**, **127** |
| 4 | **0**, **42**, **85**, **127** |
| 5 | **0**, **32**, **64**, **95**, **127** |
| 6 | **0**, **25**, **51**, **76**, **102**, **127** |

Example: SPACE MODE choices are ROOM, HALL, PLATE, MOD, SHIMMER.
PLATE is the third of five choices → **MIDI value 64**.
With SPACE in block 6, MODE is control 4 → **CC 75, value 64**.
For COMP+ PICKUP, **0 = SINGLE**, **127 = HUMBKR**.

### Time, frequency and other ranges

MIDI **0** sets the control's minimum; **127** sets its maximum. The range is
listed in the effect lookup below. Intermediate values are spread across that
range and rounded to the effect's supported steps. A frequency shown as 1.00 Hz
is not MIDI value 1 or MIDI value 100.

There are only 128 MIDI positions, so some exact pedal values cannot be reached.
For an exact value that MIDI cannot select, use the website or the pedal.
For delay timing, [tap tempo](#tap-tempo-and-midi-clock) is often easier.

### Ready-to-enter examples

Every row uses **CC / Control Change, channel 1**. These examples depend on
the named effect being in the stated block.

| What you want | Effect position | CC number | MIDI value | Result on pedal |
|---|---|---|---|---|
| COMP+ LEVEL 10 | Block 1 | **33** | **13** | LEVEL **10** |
| COMP+ LEVEL 50 | Block 1 | **33** | **64** | LEVEL **50** |
| COMP+ BLEND 70 | Block 1 | **34** | **89** | BLEND **70** |
| COMP+ PICKUP humbucker | Block 1 | **36** | **127** | PICKUP **HUMBKR** |
| DELAY MIX 25 | Block 5 | **66** | **32** | MIX **25** |
| DIGI feedback 30 | Block 5 | **65** | **38** | F.BACK **30** |
| DIGI time about 400 ms | Block 5 | **66** | **62** | TIME **398 ms** (nearest selectable value) |
| IR VOL 80 | Block 4 | **56** | **102** | VOL **80** |
| CHORUS rate 1.00 Hz | Block 6 | **72** | **24** | RATE **1.00 Hz** |
| SPACE mode Plate | Block 6 | **75** | **64** | MODE **PLATE** |

## Load a preset

Choose **PC / Program Change**, channel **1**. PC loads a whole saved preset;
it is different from CC, which changes a control or triggers an action.

| Saved preset shown on Frost | Program number sent over MIDI |
|---|---|
| 01 | **0** |
| 02 | **1** |
| 03 | **2** |
| 10 | **9** |
| 20 | **19** |
| 60 | **59** |

Some controller editors show program numbers as 1–128 while others show 0–127.
The table gives the actual number sent over MIDI. If your editor starts at 1,
its displayed program 1 normally sends MIDI program 0. Empty preset positions
and programs 60–127 are ignored; they do not select the next stored sound.

While a custom IR is loading, a new Program Change queues the latest selected
preset and cancels obsolete upload work at a safe boundary. Changed IRs still
take time to load, and preset changes include a silent gap with no tail spillover.

For navigation buttons, use **CC 87, value 127** for the next stored preset,
or **CC 88, value 127** for the previous one. Navigation skips empty positions
and wraps at the end. A value of 0 on button release does nothing.

## Other performance buttons

Choose **CC**, channel **1**:

| Button / action | CC number | MIDI value to enter |
|---|---|---|
| Tap tempo | **85** | **127** once per tap |
| Tuner ON | **86** | **127** |
| Tuner OFF | **86** | **0** |
| Next stored preset | **87** | **127** |
| Previous stored preset | **88** | **127** |
| Scene (several blocks together) | **89** | See the scene table below |
| Global bypass ON (dry sound) | **90** | **127** |
| Global bypass OFF (processed sound) | **90** | **0** |
| Follow incoming MIDI Clock | **91** | **127** |
| Stop following MIDI Clock | **91** | **0** |
| Footswitch tap tempo ON | **92** | **127** |
| Footswitch tap tempo OFF | **92** | **0** |
| Tap note 1/4 / dotted 1/8 / 1/8 | **93** | **0** / **1** / **2** |
| Footswitch tap target SELECTED | **94** | **0** |
| Footswitch tap target GLOBAL | **94** | **127** |

Set toggle buttons such as tuner and bypass to alternate **127 / 0**.
Set tap tempo and preset-navigation buttons to send **127 on press** and
nothing, or **0**, on release.

## Assign an expression pedal

The simplest assignment is to send the parameter's own CC directly. For a
delay in block 5, assign your expression pedal to CC 66 to sweep its MIX.
For overall volume, use CC 7; it multiplies the physical volume/global output
level and does not increase either setting.

Example: sweep **COMP+ LEVEL in block 1** with your expression pedal:

| Expression setting | Enter |
|---|---|
| Message type | **CC** |
| Channel | **1** |
| CC number | **33** |
| Minimum MIDI value (heel) | **0** |
| Maximum MIDI value (toe) | **127** |

This sweeps LEVEL from **0 to 100**. For a smaller sweep from LEVEL **10 to 80**,
use MIDI minimum **13** and maximum **102** instead. Moving COMP+ to another
block also changes this expression assignment's CC number.

Alternatively send **CC 80 = 4**, **CC 81 = 2**, then sweep **CC 11**. This
selects block 5, parameter 3. That target remains until reassigned or restarted;
it follows chain position across patch changes, not an effect name.
The default CC 11 target after restart is block 1, parameter 1.
Expression routing and volume are volatile and are not stored in presets.

## Tap tempo and MIDI Clock

MIDI tap tempo is the **global tap tempo**: it always sets every tempo effect
in the chain, whatever the pedal's TAP TARGET is, and it works with the
footswitch TAP TEMPO setting ON or OFF.

Tap at least twice, 200–2,000 ms apart (30–300 BPM). Each high CC 85 message
is one tap; configure one message per press, with no extra high message on
release. A gap longer than two seconds starts a new tap pair.

The tapped beat sets every delay TIME/RATE in milliseconds to one note of
GLOBAL SETTINGS → TAP TEMPO → **TAP NOTE** (a quarter note by default, or a
dotted eighth or an eighth). Modulation RATE/SPEED controls in Hz (for example
TREM, H-TREM, VIBE, WARM, PHASE) run one cycle per that note. Each control is
clamped to its supported range; a BBD delay cannot exceed its 300 ms maximum.
Reverb decay, vibrato rise, envelope release and the ring modulator frequency
are not tempo targets.

The tap tempo settings can also be changed from a controller:

| Setting | CC | MIDI value |
|---|---|---|
| Footswitch TAP TEMPO | **92** | **0–63** OFF, **64–127** ON |
| TAP NOTE | **93** | **0** = 1/4, **1** = dotted 1/8, **2** = 1/8; other values are ignored |
| Footswitch TAP TARGET | **94** | **0–63** SELECTED, **64–127** GLOBAL |

Like every MIDI command, these are not saved: they override the pedal's
GLOBAL SETTINGS → TAP TEMPO values until restart. The menu shows the value
in use; editing a setting there replaces its MIDI override and saves it with
the other settings. While following MIDI Clock, a new TAP NOTE applies at once.

For a clock source, send CC 91 = 127, then MIDI Clock. Frost accepts clock
with or without an initial Start message. It measures 24 intervals per beat
and updates effect rates when the measured beat changes by more than 2 ms.
A gap over 500 ms resets the measurement. Clock follows **tempo**, not waveform
phase; it does not restart LFO phase or synchronize delay tails sample by sample.
Busy edits are deferred. A recalled patch follows the latest clock tempo
after its load settles. Disable synchronization with CC 91 = 0.

## Scenes and global bypass

A scene lets one button switch several effects together. Set its message to
**CC**, channel **1**, **CC number 89**. The MIDI value describes which blocks
should be on. You do not need to enter binary numbers. Add the values
for the blocks you want on: block 1 = 1, block 2 = 2, block 3 = 4,
block 4 = 8, block 5 = 16, block 6 = 32. All other blocks turn off.

For example, to turn on only blocks **1, 3 and 6**, add **1 + 4 + 32 = 37**.
Configure the button for **CC 89, value 37**. Blocks 2, 4 and 5 will turn off.

| Value | Scene |
|---|---|
| 0 | Every block off |
| 15 | Blocks 1–4 on, 5–6 off |
| 47 | Blocks 1–4 and 6 on, block 5 off |
| 63 | Every occupied block on |

Empty blocks stay off. All six DSP enable targets change in one transaction,
with the normal effect bypass smoothing. Scenes preserve parameter values and
are not separately named/stored scene snapshots. Program these masks into your
controller's switches; save the resulting state as a normal preset if needed.

CC 90 global bypass crossfades the processed output to the dry input. It keeps
the current effect states and IR transport running; turning it off restores
the processed sound. Input gain, global output EQ, master output controls and safety mutes still
apply. It is software bypass, consumes the chain's normal DSP budget, and does
not recover a DSP overload or numeric fault. Global bypass is volatile and
remains active across patch changes until turned off or the pedal restarts.

## Preset recall, busy operations and saving

Program Change ignores empty preset positions. Preset up/down skips them and
wraps around stored positions. Recall uses one latest-request mailbox: if several recalls arrive
while busy, the newest replaces the queued request. A queued recall waits for
the tuner to close and current storage/asset/editor work to finish. A manual
MIDI/website recall leaves Setlist home to avoid automatic song-part recall.

Parameter, scene and individual switch messages received during startup,
preset loading or file transfers are ignored; resend after completion.
Tempo is retained for application when editing becomes available. Master
volume and global bypass requests are retained. No MIDI command saves a preset
or writes the MIDI configuration to flash. Live edits are lost on patch change
or power-off unless saved using the pedal's normal PRESETS → SAVE workflow.

For website patch changes, open **Presets → Load** beside the saved sound.
The page confirms replacement of edits made on that page and waits for the
pedal to finish loading. It requires this firmware or newer. See the
[Website Guide](Website%20Guide.md).

## Connect a controller

1. Save your presets on the GP-5 first and start at low listening volume.
2. For **M-VAVE Chocolate Plus**, connect its **USB HOST** port to the GP-5
   USB port with a data cable and select its host operating mode. Power both
   devices as their manufacturers require. The Plus has a host interface;
   see the [manufacturer's product page](https://www.sincoaudio.com/product?id=chocolate-plus)
   for its manual. Its ordinary computer/data port is a different connection.
3. For a controller without USB host capability, connect both devices to a
   computer and route the controller's MIDI output to the GP-5 MIDI input in
   MIDI routing software. Alternatively use a compatible standalone USB MIDI
   host that routes the two devices. A passive USB hub alone does not route MIDI.
4. Configure the controller for standard **PC/CC messages on channel 1**.
   Use custom MIDI mode rather than a mode intended for another manufacturer's pedal.
5. Send PC 0 to recall saved preset 01. Some controller editors display that
   program as 1: check whether their numbering starts at 0 or 1.

Frost is a USB MIDI **device**, not a USB host. DIN/TRS MIDI controllers need
a host/bridge that routes USB MIDI to it. Frost receives BLE MIDI from a
connected client when SYSTEM → BLUETOOTH is ON. Direct Bluetooth pairing with the Chocolate Plus has not been verified;
stock Valeton pairing/SysEx instructions do not establish Frost compatibility.
USB MIDI receives cable 0, channel 1; BLE MIDI uses channel 1 too. Channel 16
carries [footswitch gestures](#footswitch-gestures-press-double-press-and-hold).
Other channels are ignored.
There is no MIDI learn, channel menu, outgoing MIDI clock or parameter feedback
in this version. Do not route the GP-5's own outgoing control messages back
to its input; that can create feedback.

## Footswitch gestures: press, double press and hold

A controller can give each footswitch three actions: a **press**, a **double press**
(a second press within 0.4 s) and a **hold** (2 s). The website's **Controller** page
sets this up on an M-VAVE Chocolate Plus; any controller that can send several
messages per press can do the same. Frost recognises the gestures, so they work
on stage with the controller plugged straight into the GP-5.

The controller sends on **MIDI channel 16**: when a footswitch goes down, the twelve
bytes that describe its actions as CC 16 to 27, then **CC 28 = n** (footswitch
number 0 to 15); when it comes up, **CC 29 = n**. Nothing is stored in the pedal.
The twelve bytes are three actions (press, double press, hold) of four bytes each:

| Kind (first byte) | Then | Runs |
|---|---|---|
| 0 | — | nothing |
| 1 | CC, value | that CC on channel 1 |
| 2 | program | that program change (preset) |
| 3 | CC | a switch the pedal toggles from its own state: 86 tuner, 90 bypass all, 20 to 25 the effect at that chain position |
| 4 | CC, value A, value B | that CC with A and B in turn |
| 5 | — | the current preset's CTL effects, each switched the other way (as the GP-5's own footswitch in FOOTSWITCH CTL) |

A press runs at once when the footswitch has no double press and no hold. With a
hold it runs on release; with a double press, 0.4 s after release. A **tap tempo**
press (kind 1, CC 85) always runs on the press, so the tempo is exact; such a
footswitch has no double press. Other channels than 1 and 16 are still ignored.

A press that edits the chain or the view (the effect switches, tuner, CTL and
effect controls) waits while the pedal is busy, for example during a preset load,
and runs once it is idle. A switch (kinds 3 to 5) keeps its parity: pressed twice
while busy it leaves the effect as it was, three times it switches it once. A fixed
value (kind 1) runs once however often it was pressed.

### Two footswitches together

Two neighbouring footswitches pressed together (n and n+1, the second within
0.1 s of the first) run a fourth action instead of their own: A+B, B+C and C+D on a
four-switch controller. Footswitch n carries that pair's action, after its twelve
bytes and before its press: **CC 30 = value**, **CC 31 = value B** (each left out
when 0), then **CC 32+kind = CC or program** (kind 1 to 5, as in the table above).
A footswitch that belongs to a pair, its own or its left neighbour's, reports its
press as **CC 38 = n** instead of CC 28. Frost then waits 0.1 s for the neighbour
before running that footswitch's own press; a tap tempo still runs exactly 0.1 s
after every press, so its spacing is kept. A footswitch then sends at most 16
messages per press.

On the M-VAVE Chocolate Plus the Controller page offers **B+C** only: the
controller keeps A+B and C+D for itself (they change its group) and sends
nothing for them.

### Footswitch light colours

Also on channel 16, **CC 40 = LED ON colour** (0 to 10) and **CC 41 = LED OFF
colour** (0 to 4), in the order of GLOBAL SETTINGS → DISPLAY → LED ON / LED OFF
(0 is mint and dim red, the defaults). The footswitch shows the colour at once;
Frost saves it with the global settings 1.5 s after the last change. The website's
Controller page uses them for its colour picker.

### The website's looper on the pedal

While the website's looper is in Loop or drum mode it sends, on channel 16,
**CC 42 = state** (0 off, 1 loop mode, 2 recording, 3 overdubbing, 4 playing,
5 counting in or waiting for the bar, 6 drum mode), **CC 43 = loop position**
(0 to 127) and **CC 44 = track + 4 × section**. The footswitch light shows the state
(blue, red, orange, green, flashing white, purple) and the screen shows a looper
view over the usual one: the state in large letters, the loop's progress, the
track and the section. Without a CC 42 for 2.5 s the pedal goes back to normal.

## Reserved commands and limits

Only the commands documented here have a performance function. Notes, pitch
bend, bank select MSB, NRPN/RPN and active sensing have no performance function.
**CC 32 controls block 1, control 1**, so disable automatic bank-select LSB
messages in your controller. It is not a bank selector in Frost.
Rapid parameter changes are combined into the latest desired value; the DSP
may not apply every intermediate message. An unused control number is ignored.

## Effect control lookup

Find the effect's label below; the [Effects list](Effects%20list.md) describes
each effect. **1, 2, 3… are control positions, not CC
numbers or MIDI values.** Use those positions with the block table above.
Ranges are pedal values; named choices appear in selection order.
For example, COMP+ **2 LEVEL** means use the **Control 2** column.

### Overdrive

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| SCREAM | **1 DRIVE** (0–100); **2 TONE** (0–100); **3 LEVEL** (0–100); **4 MODEL** (CLASSIC / BRIGHT) |
| BLUESY | **1 GAIN** (0–100); **2 TONE** (0–100); **3 LEVEL** (0–100) |
| HORSE | **1 GAIN** (0–100); **2 TREBLE** (0–100); **3 OUTPUT** (0–100) |
| BREAK | **1 GAIN** (0–100); **2 TONE** (0–100); **3 VOLUME** (0–100) |
| OBSESS | **1 DRIVE** (0–100); **2 TONE** (0–100); **3 VOLUME** (0–100); **4 PEAK** (LP / HP) |
| DRIVE | **1 GAIN** (0–100); **2 TONE** (0–100); **3 VOL** (0–100) |
| RANGER | **1 BOOST** (0–100); **2 RANGE** (0–100); **3 LEVEL** (0–100) |
| BOOST | **1 GAIN** (0–20 dB); **2 BRIGHT** (OFF / ON) |

### Distortion

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| ORANGE | **1 DIST** (0–100); **2 TONE** (0–100); **3 LEVEL** (0–100) |
| MOUSE | **1 DIST** (0–100); **2 FILTER** (0–100); **3 VOLUME** (0–100) |
| PLUS | **1 DIST** (0–100); **2 OUTPUT** (0–100) |
| FACE | **1 FUZZ** (0–100); **2 VOL** (0–100); **3 BIAS** (0–100) |
| BIG PI | **1 SUSTAIN** (0–100); **2 TONE** (0–100); **3 VOLUME** (0–100) |
| CRUSH | **1 BITS** (2–16); **2 RATE** (1–44.1 khz); **3 MIX** (0–100) |

### Delay

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| DIGI | **1 E.LEVEL** (0–100); **2 F.BACK** (0–100); **3 TIME** (13–800 ms); **4 HOLD** (OFF / ON); **5 FOOT** (BYPASS / HOLD); **6 TAIL** (OFF / ON) |
| BBD | **1 RATE** (20–300 ms); **2 ECHO** (0–100); **3 INTENS** (0–100); **4 TAIL** (OFF / ON) |
| DELAY | **1 TIME** (1–500 ms); **2 REPEAT** (0–100); **3 MIX** (0–100) |
| TAPE | **1 TIME** (20–800 ms); **2 REPEAT** (0–100); **3 MIX** (0–100); **4 WOW** (0–100); **5 AGE** (0–100); **6 DRIVE** (0–100); **7 TAIL** (OFF / ON) |
| PING | **1 TIME** (20–400 ms); **2 F.BACK** (0–100); **3 E.LEVEL** (0–100); **4 WIDTH** (0–100); **5 TAIL** (OFF / ON) |

### Reverb

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| SPACE | **1 E.LEVEL** (0–100); **2 TONE** (0–100); **3 TIME** (0–100); **4 MODE** (ROOM / HALL / PLATE / MOD / SHIMMER); **5 TAIL** (OFF / ON) |
| SPRING | **1 DWELL** (0–100); **2 MIX** (0–100); **3 TONE** (0–100); **4 TAIL** (OFF / ON) |
| REVERB | **1 DECAY** (0–100); **2 TONE** (0–100); **3 MIX** (0–100) |

### Modulation

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| PHASE | **1 SPEED** (0.1–10 hz); **2 VOICE** (SCRIPT / BLOCK) |
| WARM | **1 RATE** (0.1–8 hz); **2 DEPTH** (0–100) |
| LADY | **1 RATE** (0.02–2 hz); **2 RANGE** (0–100); **3 COLOR** (0–100); **4 MATRIX** (OFF / ON) |
| JET | **1 MANUAL** (0–100); **2 WIDTH** (0–100); **3 SPEED** (0.05–5 hz); **4 REGEN** (0–100) |
| VIBE | **1 SPEED** (0.3–10 hz); **2 INTENS** (0–100); **3 MODE** (CHORUS / VIBRATO); **4 VOLUME** (0–100) |
| WARBLE | **1 RATE** (0.1–10 hz); **2 DEPTH** (0–100); **3 RISE** (150–5000 ms); **4 MODE** (LATCH / UNLATCH) |
| TREM | **1 RATE** (0.3–12 hz); **2 WAVE** (0–100); **3 DEPTH** (0–100) |
| H-TREM | **1 RATE** (0.3–12 hz); **2 DEPTH** (0–100); **3 LEVEL** (0–100) |
| ROTARY | **1 SPEED** (SLOW / FAST); **2 DEPTH** (0–100); **3 BALANCE** (0–100); **4 LEVEL** (0–100) |
| RING | **1 FREQ** (20–2000 hz); **2 MIX** (0–100) |
| CHORUS | **1 RATE** (0.05–5 hz); **2 DEPTH** (0–100); **3 MIX** (0–100) |
| FLANGE | **1 RATE** (0.05–5 hz); **2 DEPTH** (0–100); **3 RESO** (-100–100) |
| ENVWAH | **1 SENS** (0–100); **2 MANUAL** (0–100); **3 DECAY** (10–1000 ms); **4 MODE** (UP / DOWN / SHARP / HUMAN / TEMPO); **5 VOWEL 1** (A / E / I / O / U); **6 VOWEL 2** (A / E / I / O / U); **7 RATE** (0.25–5 hz); **8 MIX** (0–100) |
| FOXY | **1 SPEED** (0.05–8 hz); **2 DEPTH** (0–100); **3 REGEN** (0–100); **4 STAGES** (6 STAGE / 10 STAGE); **5 MANUAL** (0–100); **6 MODE** (AUTO / MANUAL); **7 MIX** (0–100) |

### Dynamics / Eq

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| SQUASH | **1 OUTPUT** (0–100); **2 SENS** (0–100) |
| BLEND | **1 SUSTAIN** (0–100); **2 LEVEL** (0–100); **3 BLEND** (0–100); **4 TONE** (0–100); **5 PICKUP** (SINGLE / HUMBKR) |
| EQ-7 | **1 100** (-15–15 dB); **2 200** (-15–15 dB); **3 400** (-15–15 dB); **4 800** (-15–15 dB); **5 1.6K** (-15–15 dB); **6 3.2K** (-15–15 dB); **7 6.4K** (-15–15 dB); **8 LEVEL** (-15–15 dB) |
| CALIEQ | **1 80** (-12–12 dB); **2 240** (-12–12 dB); **3 750** (-12–12 dB); **4 2.2K** (-12–12 dB); **5 6.6K** (-12–12 dB); **6 LEVEL** (-12–12 dB) |
| NOISE | **1 THRESH** (0–100); **2 DECAY** (0–100); **3 MODE** (REDUCE / MUTE) |
| NOISEX | **1 THRESH** (0–100); **2 DECAY** (0–100); **3 DAMP** (0–100); **4 MODE** (REDUCE / GATE / MUTE) |
| MB-CMP | **1 LOW THR** (-60–0 dB); **2 MID THR** (-60–0 dB); **3 HI THR** (-60–0 dB); **4 RATIO** (1–10); **5 ATTACK** (1–100 ms); **6 RELEASE** (20–1000 ms); **7 MIX** (0–100); **8 LEVEL** (0–100) |
| SWELL | **1 ATTACK** (50–3000 ms); **2 SENS** (0–100); **3 LEVEL** (0–100) |

### Utility / Cab

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| GAIN | **1 GAIN** (0–100) |
| GATE | **1 THRESH** (0–100) |
| EQ | **1 BASS** (0–100); **2 MID** (0–100); **3 TREBLE** (0–100) |
| IR | **1 VOL** (0–100); **2 LOW CUT** (0–400 hz); **3 HIGH CUT** (1000–20000 hz) |
| HPF | **1 FREQ** (20–18000 hz); **2 Q** (0.5–2); **3 SLOPE** (12 DB / 24 DB) |
| LPF | **1 FREQ** (20–18000 hz); **2 Q** (0.5–2); **3 SLOPE** (12 DB / 24 DB) |
| ACOUST | **1 BODY** (0–100); **2 TOP** (0–100); **3 MODE** (STANDARD / JUMBO / PIEZO / ENHANCE); **4 MIX** (0–100); **5 LEVEL** (0–100) |

### Amp / Capture

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| US-DLX | **1 GAIN** (0–100); **2 BASS** (0–100); **3 MID** (0–100); **4 TREBLE** (0–100); **5 BRIGHT** (0–100); **6 MASTER** (0–100); **7 LEVEL** (0–100) |
| US-DBL | **1 GAIN** (0–100); **2 BASS** (0–100); **3 MID** (0–100); **4 TREBLE** (0–100); **5 BRIGHT** (0–100); **6 MASTER** (0–100); **7 LEVEL** (0–100) |
| CHIME | **1 GAIN** (0–100); **2 BASS** (0–100); **3 MID** (0–100); **4 TREBLE** (0–100); **5 CUT** (0–100); **6 MASTER** (0–100); **7 LEVEL** (0–100) |
| JAZZ | **1 GAIN** (0–100); **2 BASS** (0–100); **3 MID** (0–100); **4 TREBLE** (0–100); **5 BRIGHT** (0–100); **6 MASTER** (0–100); **7 LEVEL** (0–100) |
| UK-800 | **1 GAIN** (0–100); **2 BASS** (0–100); **3 MID** (0–100); **4 TREBLE** (0–100); **5 PRESENCE** (0–100); **6 MASTER** (0–100); **7 LEVEL** (0–100) |
| CALI | **1 GAIN** (0–100); **2 BASS** (0–100); **3 MID** (0–100); **4 TREBLE** (0–100); **5 PRESENCE** (0–100); **6 MASTER** (0–100); **7 LEVEL** (0–100) |
| BROWN | **1 GAIN** (0–100); **2 BASS** (0–100); **3 MID** (0–100); **4 TREBLE** (0–100); **5 PRESENCE** (0–100); **6 MASTER** (0–100); **7 LEVEL** (0–100) |
| SL-100 | **1 MODE** (CLEAN / CLEAN BRT / CRUNCH / CRUNCH BRT / LEAD); **2 GAIN** (0–100); **3 BASS** (0–100); **4 MID** (0–100); **5 TREBLE** (0–100); **6 PRESENCE** (0–100); **7 MASTER** (0–100); **8 LEVEL** (0–100) |
| NAM | **1 MODEL** (0–63); **2 GAIN** (-12–12 dB); **3 LEVEL** (0–100); **4 BASS**, **5 MIDDLE**, **6 TREBLE** (-12–12 dB each) |

### Pitch

| Effect label | Controls in order (pedal range or choices) |
|---|---|
| DROP | **1 DROP** (1/2 STEP / 1 STEP / 3/2 STEP / 2 STEP / 5/2 STEP / 3 STEP); **2 MIX** (0–100) |
| OCTAVE | **1 DRY** (0–100); **2 SUB OCT** (0–100); **3 OCT UP** (0–100) |

## Raw MIDI examples (advanced)

These are MIDI bytes (hex), not USB packets. MIDI routing tools and controller
editors normally construct USB packets themselves.

| Bytes | Meaning |
|---|---|
| `C0 00` | Load preset 01 |
| `B0 14 7F` / `B0 14 00` | Block 1 on / off |
| `B0 42 40` | Block 5 parameter 3 to roughly halfway (CC 66, value 64) |
| `B0 07 7F` | Full expression volume |
| `B0 55 7F` | One tap-tempo press |
| `B0 56 7F` / `B0 56 00` | Tuner on / off |
| `B0 57 7F` / `B0 58 7F` | Next / previous preset |
| `B0 59 2F` | Scene mask 47 |
| `B0 5A 7F` / `B0 5A 00` | Global bypass on / off |
| `B0 5B 7F`, then `F8` ticks | Enable and receive MIDI Clock |
| `B0 5C 7F` / `B0 5C 00` | Footswitch tap tempo on / off |
| `B0 5D 01` | Tap note dotted 1/8 |
| `B0 5E 7F` / `B0 5E 00` | Footswitch tap target global / selected |

USB-MIDI examples on cable 0: PC 0 = `0C C0 00 00`, block 1 on =
`0B B0 14 7F`, clock = `0F F8 00 00`. Frost's website SysEx protocol remains
separate and works alongside these standard messages.

### Website protocol additions (developers)

These are payload bytes inside the existing GP-5 nibble/CRC SysEx framing,
not raw MIDI bytes to send directly. `web/pedal.js` builds the outer frame;
`web/live.js` implements recall and completion polling. Numbers below are hex.

| Payload | Reply payload |
|---|---|
| `7E 1C` | `7E 1C 01 current queued busy error bypass volume ready` |
| `7E 1D index` | `7E 1D accepted`; queue preset index 0–59, accepted = 0/1 |
| `7E 1E` | `7E 1E 01` followed by null-terminated `FROST_FW:` and semantic version |
| `7E 1F o0 … o5` | `7E 1F accepted`; reorders the live chain, `o[position]` = slot (a permutation of 0–5); settings stay with their blocks (Frost 32p+) |
| `7E 20 index [name]` | `7E 20 accepted`; saves the live sound as preset index 0–59, with an optional name of 1–16 ASCII characters (Frost 32p+) |
| `7E 20` | `7E 20 01 busy error current reason retries`; save status: poll until busy is zero; error non-zero means the save failed. Reason: 2 busy, 3 revision, 4 checksum (the sound changed while it was read back), 5 storage, 1 other; the read-back restarts up to 4 times before failing |

In status, current/queued are zero-based preset numbers; `FF` means none.
Busy covers preset recall and IR asset work. Error is the preset client's
error code (zero means no error); ready also requires the editor and global
settings to settle. Queue acceptance is not completion. Send a load once,
then poll until queued is `FF`, busy is zero, ready is one and current matches
the requested index. A lost reply does not authorize retrying a recall: the
first request may already have loaded the sound. The website does not retry it.

If a command has no effect, check channel 1, controller custom mode, USB host
routing, preset existence, block order and whether the pedal is busy. Stop
other routed MIDI sources before investigating unexpected changes.
