# Frost

**Custom guitar effects firmware for the Valeton GP-5.**

[Installation](docs/Installation%20Guide.MD) · [Pedal guide](docs/User%20Guide.md) · [Website guide](docs/Website%20Guide.md) · [MIDI commands](docs/Midi%20Command.md) · [Build firmware](docs/Build%20Firmware.md)

## What is Frost?

Frost is alternative firmware for the Valeton GP-5, with an editable signal
chain, amp models, cabinet impulse responses and presets. Shape your sound
with the pedal's controls or use the companion website to edit effects,
manage models and cabinets, and organize songs for performance.

Current source version: **0.1.0-beta.7**. Settings → Firmware displays
**0.1.0-BETA.7**. This candidate reduces global EQ and amp-editing overhead and requires pedal listening and load checks before release. Frost is in beta and supports the Valeton GP-5.

## Features

- **Six-slot signal chain:** select effects, adjust parameters, change their
  order and bypass individual blocks.
- **52 effect and amp types:** drives, distortion, dynamics, EQ, modulation,
  delay, reverb, pitch effects, acoustic simulation and seven amp-head voices.
- **Capture models:** convert supported NAM A2 captures into Frost FCM models
  and manage up to 64 model positions through the website.
- **Cabinet IRs:** a built-in cabinet plus 63 custom cabinet positions.
- **60 editable presets:** 20 factory sounds on first use and space for your
  own sounds. Existing Frost presets survive firmware updates.
- **Four home screens:** Chain, Stomp, Preset and Setlist.
- **Songs and setlists:** assign presets to song parts and advance them with
  the footswitch.
- **Tuner and global settings:** tuning reference, input/output levels,
  cabinet override, three-band global output EQ and display preferences.
- **USB MIDI:** preset recall, every effect’s parameters and switches, expression,
  tap tempo, tuner, scene masks, global bypass and incoming MIDI Clock.
  See the [MIDI guide](docs/Midi%20Command.md) for mappings and USB host requirements.
- **Browser tools:** live editing, local model conversion, IR import, preset
  management and patch loading, backups and USB firmware installation.

Frost plays converted capture approximations; it does **not** run native NAM.
Heavy chains can exceed the DSP budget. Preset changes do not provide
effect-tail spillover, and storage writes can briefly interrupt audio.

## Getting started

1. Follow the [Installation Guide](docs/Installation%20Guide.MD) to back up
   your sounds and install the matching firmware file.
2. Read the [User Guide](docs/User%20Guide.md) for controls, presets, effects,
   tuning and performance workflows.
3. Open [Frost Studio](https://frost.fretlexwins.workers.dev/) and follow the
   [Website Guide](docs/Website%20Guide.md) to connect and manage your pedal.

Developers: see [Build Firmware](docs/Build%20Firmware.md) and
[CONTRIBUTING.md](CONTRIBUTING.md).
To write a custom effect, follow [Build an effect](docs/Build%20effect.MD).

## License

Frost is licensed under the [MIT License](LICENSE). Adapted components retain
their notices in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
Private captures, user IR files, compiler archives and stock firmware are not
distributed as source. Valeton and GP-5 are trademarks of their respective
owners; Frost is an independent project.

## Support the project

If Frost is useful to you, you can help support its development:

**[Support Frost on Ko-fi](https://ko-fi.com/freelender)**

## Credits

Frost builds on **[OpenGP5](https://github.com/OpenGP5/OpenGP5)**. Thank you to
its contributors for the GP-5 platform, hardware interfaces, USB firmware
tools and shared research that made Frost possible.

Thanks also to everyone testing Frost and reporting reproducible issues.
