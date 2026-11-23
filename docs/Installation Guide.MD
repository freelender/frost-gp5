# Installation Guide

Install Frost on a **Valeton GP-5** through USB. You do not need to build the
source if you have a supplied firmware file.

## 1. Prepare

You need a powered GP-5, a USB **data** cable, desktop Chrome or Edge, and
the matching Frost `.bin` extracted from your release/tester ZIP.
Open [Frost Studio](https://frost.fretlexwins.workers.dev/).

Frost replaces the pedal's application firmware. Keep your sounds backed up
and read the recovery steps below before installing.

## 2. Back up your sounds

1. For stock Valeton firmware, use Valeton's editor to back up stock presets.
2. For an existing Frost installation, connect in Frost Studio, open
   **Presets → Back up everything**, and keep the `.frostbackup` file.
3. Keep stock and Frost backups separately; they are different formats.

Firmware updates preserve existing Frost storage. Backups do not include
unsaved live edits, so save those to a preset first.

## 3. Connect

1. Turn listening volume down and power the GP-5 normally.
2. Connect the pedal directly to the computer with the data cable.
3. Close other pedal editors and browser tabs using the GP-5.
4. Open Frost Studio and allow MIDI/SysEx access when asked.

If no MIDI device appears, check browser permission and try another cable or
USB port. Charge-only cables cannot transfer firmware.

## 4. Choose the firmware

1. Open **Firmware** in the website sidebar.
2. Click **Drop a firmware .bin**, or drag the extracted `.bin` into the box.
3. Read the detected version and section list before proceeding.

The supplied first beta is `Frost_GP5_0.1.0-beta.1.bin`, 506,976 bytes.
MIDI and website patch recall require **0.1.0-beta.2 or newer**. The current
source candidate, **0.1.0-beta.7**, adds NAM Bass/Middle/Treble and global output EQ, retaining the reverb and preset-recall improvements.
It still needs physical listening/load checks before release. Verify the settings version
against the package you actually install.
The first beta's SHA-256 is:

```text
614c554ac9807af395a8c821e61919d5f4d472e66b7d1fcd38f502bc656a6c11
```

A custom source build has its own checksum. Use its build manifest and do
not assume it matches the supplied image. A ZIP, `.nam`, WAV or backup is
not a firmware file.

Some website releases offer a bundled **Install Frost** button. If it is
unavailable, choose the supplied local `.bin`; separate-file beta delivery
does not require a firmware download on the site.

## 5. Install

1. Click the install button for your selected image.
2. Read and approve the confirmation for the intended GP-5.
3. Leave power and USB connected. Update mode is entered automatically when
   possible. Allow around 90 seconds for the complete image.
4. Wait for completion and the pedal's restart.
5. Reconnect in the website; reload the page if needed.

The Frost image contains DSP, DSP-data and controller app sections. It does
not replace the Frost user-preset/model-storage area. Do not unplug during
transfer. If only reconnection fails, inspect the pedal first rather than
immediately repeating a completed flash.

## 6. Check the result

1. Hold **PARA** on a home screen to open Settings.
2. Find **FIRMWARE**. The numbered beta displays **0.1.0-BETA.1**.
3. Choose **BACK** to leave Settings.
4. At low volume, select a preset and play through the normal audio output.
   USB is used here for control and file transfer.
5. Try a patch with IR enabled, then change Chain → Preset and Preset → Chain.
   Confirm audio resumes after saving the home-screen setting.

First use with empty Frost storage supplies 20 editable factory presets.
An existing Frost bank is retained. A firmware update does not need a factory
reset; that reset erases user data.

## Recovery

1. Disconnect pedal power.
2. Hold the **footswitch and PARA knob** while reconnecting power to enter
   the factory update mode.
3. Keep USB connected, open the website's Firmware page and choose the
   intended firmware file.
4. Install again and wait for completion. Check power, cable and other MIDI
   applications if communication fails.

To return to stock, obtain the correct official GP-5 image from Valeton and
choose it in the same installer. Stock firmware is not distributed here.
Use your stock backup with the stock editor; a Frost backup is not converted
into Valeton presets by changing firmware.

## Troubleshooting

| Problem | Check |
|---|---|
| No pedal listed | Desktop Chrome/Edge, data cable, MIDI permission and competing editors closed |
| File rejected | Extract the ZIP and select its GP-5 `.bin`; copy/download again if corrupted |
| Old website information | Hard reload with Ctrl+Shift+R and select the file again |
| Interrupted update | Use the factory update-mode procedure above |
| "Refused the erase" (error code 01) every time, nothing written | Unplug and reconnect: the pedal keeps its current firmware. Newer units with a locked bootloader reportedly refuse every update, Valeton's own updater included; Frost cannot be installed on them |
| "Did not answer the erase" (no answer), nothing written | Unplug and reconnect, then retry with another data cable, no hub and other MIDI applications closed |
| Transfer finished, reconnect failed | Check the pedal, reconnect USB and reload before considering another update |

Next: [User Guide](User%20Guide.md) · [Website Guide](Website%20Guide.md).
