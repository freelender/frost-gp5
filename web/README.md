# Frost website source

The user-facing guide is [Website Guide](../docs/Website%20Guide.md).
Serve this directory over localhost or HTTPS; opening index.html as a file
does not provide a supported Web MIDI connection.

| Files | Responsibility |
|---|---|
| index.html, style.css | Page structure and approved Frost appearance |
| app.js | Navigation, connection state and serialized transfers |
| model_library.js | Combined model/IR routing, file detection, search and explicit block selection |
| live.js | Six-slot live chain, effect inspector and preset recall/status |
| pedal.js, store.js | USB-MIDI transport and packed pedal storage |
| setlist.js, setlist_view.js | Song library, performance order and browser drafts |
| ir.js | WAV cabinet import and conversion |
| convert_worker.js, converter/ | Browser-local capture conversion |
| firmware.js, flasher/ | Image recognition and USB firmware installation |
| sim.js | Demo pedal; no real device writes |
| effects.json | Generated effect/control catalog |

After changing effect metadata, regenerate effects.json with
`python tools/export_effects.py` from the repository root. After changing
shared JavaScript, keep entry/import cache versions aligned. Browser tests
under tests/js/ check the storage, conversion and flasher behavior.

Documentation images in assets/docs/ are optimized browser screenshots of the
current source. Populated examples use a local documentation fixture with
synthetic names; the normal demo still has no storage. Keep original captures,
fixtures and image-processing tools outside Frost, and update screenshot
captions when a workflow changes. Never use private captures or pedal data.

No firmware, capture or cabinet file is bundled in this directory. The
firmware manifest's file field stays empty for separate beta delivery. The
uploader recognizes registered release hashes after package validation;
custom source builds can have different hashes and identification strings.

The GitHub Pages workflow is manual. Publishing this directory does not
publish the rest of the repository, build output or private assets.
