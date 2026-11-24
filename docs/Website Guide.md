<p align="center"><img src="../assets/branding/frost_logo.webp" alt="GP-5 Frost" width="340"></p>

# Website Guide

[Frost Studio](https://frost.fretlexwins.workers.dev/) is the companion editor
for the GP-5. It manages the pedal through USB-MIDI. The pedal can play its
saved presets and setlist without a browser connected.

## Connect or try the demo

1. Use desktop Chrome or Edge. Power the pedal and connect a USB data cable.
2. Close other editors and tabs connected to the GP-5.
3. Open Frost Studio, click **Connect GP-5** and allow MIDI/SysEx access.
4. Wait until the connection status shows the pedal is connected.

The status bar distinguishes disconnected, connecting, connected, busy, demo
and error states. During file transfers, wait for the operation to finish
before editing or starting another transfer. If USB disconnects, reconnect
and read the pedal again. Hard reload with Ctrl+Shift+R after a website update.

**Try the demo** opens a simulated live editor without writing a real pedal.
It is useful for exploring the interface, not for judging the sound or testing
real hardware compatibility. Use the connection control to disconnect when
finished. Listen through the GP-5's normal audio output; this editor is not a
USB audio recorder. Frost does not support the GP-5's USB audio interface or
Bluetooth yet: USB carries only MIDI, for this website and for controllers.

<p align="center"><img src="../assets/docs/website-connect.webp" alt="Frost Studio before connecting: Connect GP-5 or Try the demo" width="760"><br><sub>Frost Studio before connecting: Connect GP-5 or Try the demo</sub></p>

## Live editor

<p align="center"><img src="../assets/docs/website-live-editor.webp" alt="Live editor: the signal chain from guitar input to output, the selected block's knobs, and the Effects library (right)" width="760"><br><sub>Live editor: the signal chain from guitar input to output, the selected block's knobs, and the Effects library (right)</sub></p>

The **◀ ▶** arrows at the top of the Live editor load the previous or next
stored preset; between them is the preset now playing. Click its name to open
a list of every stored preset with a search box (name or number); one click
loads a preset, and ↑/↓, Enter and Esc work from the keyboard. Like **Load**,
the arrows and the list ask first when you have unsaved changes from this page.

The chain strip shows the six slots in input-to-output order. Click a block
to select it; the inspector below shows its controls. Left/right keyboard
arrows also move the selection between blocks.

To change the order, **drag a block** onto another position. The other blocks
keep their order and every block keeps its settings. On a touch screen or with
the keyboard, use the inspector's **◀ ▶** buttons or **Alt + ←/→**. Reordering
needs Frost 32p or later; like every live edit it is not saved until you save
a preset.

### Choose and edit an effect

1. Select a block.
2. In the **Effects library** on the right, search or pick a category, then
   click an effect to load it into the selected block. You can also **drag an
   effect** from the library onto any block.
3. A newly chosen effect starts off. Use its switch to enable it.
4. Turn its knobs: drag up or down, or scroll on a knob (hold Shift for fine
   steps); double-click a knob for its default; the arrow keys also work on a
   focused knob. Labelled choices are drop-downs. Changes play immediately.
5. Use **Remove** to clear the block after confirming.

The pedal can refuse an unsupported combination, such as a second amp or a
resource configuration it cannot accept; the previous block then remains.
When it refuses an effect because the chain would overload the audio processor,
the page shows the same **DSP LIMIT** popup as the pedal's screen (or **CHAIN
TOO BIG** when the effects do not fit its effect memory). Remove an effect or
choose a lighter one. The popup needs Frost 0.1.0-beta.12 or later.
Six visible slots do not guarantee that every heavy chain fits the DSP budget.
See the [Effects list](Effects%20list.md) for every effect and its controls,
and the [User Guide](User%20Guide.md) for pedal-side position changes.

### Cabinet selector

An IR block has an additional **CAB** selector. CAB 00 is built in; 01–63
refer to imported cabinets. The status explains whether the cabinet is loading,
missing or globally bypassed. An empty/missing custom cabinet uses the built-in
fallback. Uploading a cabinet does not select it automatically.

### Save your live sound

Live edits change the current sound but are **not stored in a preset**.
To keep them from the website (Frost 32p or later):

1. In **Live vs saved**, choose **Save as preset**: a preset number. It starts
   on the preset currently loaded; numbers marked *(replace)* are in use.
2. Enter a **Name** (up to 16 characters) and click **Save to pedal**.
3. Confirm. The pedal saves the sound exactly as its own PRESETS → SAVE does.

On the pedal: press PARA on a selected block, open **PRESETS**, choose the
destination number, then **SAVE**. Saving to an occupied number replaces it.

Changing presets or switching off before saving discards live edits. A backup
contains stored data, not unsaved live settings.

## AMP models

This page converts supported **NAM A2 WaveNet** captures to Frost FCM models
and writes ready-made `.fcm` files. Unsupported architectures/layouts are
rejected. The resulting sound is an approximation of the capture; Frost does
not run native NAM on the pedal.

### Convert a capture

1. Drop a supported `.nam` into the model box or choose it from your computer.
2. Set a name (up to 15 characters) and destination **MODEL** number.
3. Optionally choose the recommended training `input.wav` described by the
   page. **Clear** removes that optional audio selection.
4. Click **Convert** and wait for the progress display. Conversion can take
   several minutes. Keep the tab open; **Cancel** stops conversion.
5. Review the completion message, then click **Write to pedal** and confirm
   the destination. Wait for the transfer to finish.
6. Select the matching MODEL in the pedal's **NAM** block and save the preset.

Conversion runs in browser workers; the selected capture is not uploaded to a
conversion server. A ready-made `.fcm` does not need NAM conversion: select
it, choose its destination and write it when the page enables that action.

MODEL 00 provides the built-in/default behavior; user model positions are
01–63. The **On the pedal** list shows stored models. **Remove** deletes the
selected file after confirmation. Presets reference numbers: replacing or
removing a model affects every preset using that number. A missing model
uses the default rather than restoring the deleted capture.

## Cabinet IRs

1. Drop a cabinet `.wav` into the import box.
2. Read the parsed-file information, enter a name (up to 15 characters) and
   choose **CAB 01–63**.
3. Click **Write to pedal**, confirm any replacement and wait.
4. In the Live editor, select an IR block and choose that CAB number. Enable
   the block and save the preset on the pedal.

The importer accepts supported WAV sample formats/rates and mono/stereo
input, converts to mono 44.1 kHz and prepares a 512-tap cabinet response.
This is for short cabinet responses, not preserving long room/reverb tails.
CAB 00 remains built in and is not a writable user position.

The stored list offers **Remove**. Replacing a CAB changes every preset
referencing it; deleting it leaves those references with built-in fallback.
If a cabinet seems bypassed, check the block switch and the pedal's Global
**CAB OVERRIDE** setting.

## Presets

The list shows saved pedal presets, not a separate browser preset bank. Type
in the search box above it to show only the presets whose name or number
matches. The preset now playing is marked "playing now".

- **Load:** play that saved preset. Unsaved live edits are replaced; the page
  asks first when it knows you edited from this page. It waits for recall to
  finish and refreshes the chain. Close the tuner and finish file transfers
  first. Edits made only on the pedal
  cannot be detected reliably by the page's unsaved-edit indicator.
- **Rename:** enter the new name (up to 16 characters) and confirm.
- **Up/down:** swap a preset with the number above or below it. The list
  changes at once; the pedal follows in about a second.
- **Delete:** remove that stored preset after confirmation.
- **Download:** save that preset as a `.frostpreset` file, for sharing or
  keeping single sounds.

To **import** a preset file, choose the destination in **Import into**, then
**Import preset** and the file. Importing to a used number replaces that preset
after confirmation. A preset file holds the chain, order and every setting.
Cabinets and AMP models are referenced by their CAB and MODEL numbers, not
included: the receiving pedal needs the same files at those numbers, otherwise
the built-in cabinet or default model plays.

Song parts reference preset numbers. Reordering, replacing or deleting a
preset changes what those parts play. Check your setlist after reorganizing.
To save live edits, use **Save to pedal** in the Live editor, or the on-pedal
controls described in the [User Guide](User%20Guide.md). MIDI controllers can also recall patches and
control every effect: see [MIDI commands](Midi%20Command.md).

### Back up everything

Click **Back up everything** and keep the downloaded `.frostbackup` file.
It contains stored presets, AMP models, IRs, global settings and songs/setlist.
Use a dated backup before firmware updates or large reorganizations.

### Restore backup

Choose **Restore backup**, select a Frost backup and read the confirmation.
Restore replaces pedal data with the backup's contents; back up the current
state first. Wait for completion and restart after restoring global preferences.
A Frost backup is neither a firmware image nor a Valeton preset backup.

## Songs & setlist

There are two separate things: a **song library** of named parts and a
**performance order** that arranges songs on the pedal.

1. Save the presets your parts will use.
2. Open **Songs & setlist**. To preserve an existing pedal library, start with
   **Read from pedal**. Confirm only if you want to replace the browser draft.
3. Click **New song**, enter its name and name its first part.
4. Choose a saved preset number for each part. **Add part** creates another
   part; the up/down controls reorder parts and **Remove** removes one.
5. Click **Add to setlist**. A library song alone is not in performance order.
6. Name the setlist. Use its up/down controls to arrange the performance order.
   **Remove** here removes an order entry, while **Delete song** removes the
   library song and its entries. A song can appear in the order more than once.
7. In the **Setlist** panel click **Save setlist to pedal**, confirm and wait
   for the display to refresh.
8. On the pedal choose GLOBAL SETTINGS → **DISPLAY → HOME SCREEN → SETLIST**, then **BACK** to save.

**Remove setlist from pedal** (in the Setlist panel) deletes the songs and
setlist stored on the pedal after confirmation; its Setlist home screen then
shows NO SETLIST. Presets and global settings are kept, and the browser draft
stays so you can save it again.

**Export songs** saves the browser library/order as JSON. **Import songs**
replaces the browser draft after validation/confirmation; it does not write
the pedal until you choose Save setlist to pedal. Drafts also persist in this browser's
local storage. Other browsers/computers have separate drafts.

Limits: up to 64 library songs, 16 parts per song and 64 performance entries,
subject to the shared byte limit shown on the page. Use the usage indicator
and validation message when a large library does not fit. There is one active
setlist on the pedal; exported JSON files can keep alternatives.

## Firmware

Use this page to select a local `.bin`, review its identity/sections and
confirm installation. A bundled Install Frost button is available only when
that site copy includes a firmware download. The installer also accepts the
correct official Valeton image you supply for restoring stock firmware.

Follow the [Installation Guide](Installation%20Guide.MD) for backups,
installation, verification and recovery. Keep power/USB connected until the
operation finishes; the progress log reports section writes and restart.

## About

The About page describes Frost, credits the project it builds on and provides
the Ko-fi support link (ko-fi.com/freelender). It does not change pedal data.

## Quick troubleshooting

| Symptom | Action |
|---|---|
| Connect fails | Check desktop browser, data cable, MIDI/SysEx permission and competing editors |
| Controls disabled | Connect the pedal and wait for any current transfer |
| Edits disappear | Use the pedal's PRESETS → destination → SAVE workflow |
| Model conversion rejected | Check that the capture is a supported NAM A2 layout |
| Uploaded cabinet not heard | Select its CAB in IR, enable the block, disable CAB OVERRIDE |
| NO SETLIST on the pedal | Add songs to performance order and Save to pedal |
| Unexpected cached behavior | Hard reload and reconnect |

Storage writes can briefly interrupt sound. Prepare files and settings before
performance; preset/part recall is the performance workflow.

MODEL and CAB uploads retry a temporarily refused flash erase/write up to
three times with a short delay. Data is read back before committing the new
file. If an upload still reports `program failed at 0x...` or `erase failed`,
keep the full error and firmware version for a bug report. During replacement,
the previous stored copy is retained until the new data and header are verified.
