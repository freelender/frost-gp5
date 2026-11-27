<p align="center"><img src="../assets/branding/frost_logo.webp" alt="GP-5 Frost" width="340"></p>

# Build Firmware

This guide builds Frost from source without accessing a pedal. Run commands
from the repository root. Keep compilers, stages and output in an external
work directory so the source checkout and its `build/` folder remain clean.

## 1. Supported build environment

Firmware compilation needs **Linux x86_64**. On Windows, use Ubuntu through
WSL2. The NDS32 compiler is a 32-bit Linux executable, so its compatibility
runtime is required. Native Windows Python can run host tests and USB-MIDI
tools, but does not replace the Linux firmware compilers. A native macOS or
ARM Linux target build is not provided by this toolchain setup.

Install prerequisites in Ubuntu:

```sh
sudo apt update
sudo apt install -y git python3 python3-venv python3-pip clang cmake \
  build-essential tar xz-utils curl ca-certificates libc6-i386
```

For browser tests, use Node.js 22 or newer (installation commands below).
The website is static JavaScript and needs no npm installation or bundling.

If starting on Windows, install WSL/Ubuntu with `wsl --install -d Ubuntu`
from an administrator terminal, restart if prompted, and run the remaining
Linux commands inside Ubuntu. Keep shell scripts with LF line endings; the
repository's `.gitattributes` specifies this.

## 2. Get the source and create a work directory

Download/clone this repository, open its root directory, then create a
separate work folder. The commands below do not require a particular GitHub
owner or repository name.

```sh
export FROST_WORK="$(cd .. && pwd)/frost-work"
mkdir -p "$FROST_WORK"
python3 -m venv "$FROST_WORK/venv"
. "$FROST_WORK/venv/bin/activate"
python -m pip install --upgrade pip
```

`FROST_WORK` must be outside this source repository. The first two commands
can be adapted to an existing external directory.

Install the verified Node.js 22 release into that work directory if you do
not already have a suitable Node version. These are official binaries from
the [Node.js download site](https://nodejs.org/en/download):

```sh
mkdir -p "$FROST_WORK/node"
curl -fL https://nodejs.org/dist/v22.23.5/node-v22.23.5-linux-x64.tar.xz \
  -o "$FROST_WORK/node-v22.23.5-linux-x64.tar.xz"
curl -fL https://nodejs.org/dist/v22.23.5/SHASUMS256.txt \
  -o "$FROST_WORK/node-SHASUMS256.txt"
(cd "$FROST_WORK" && sha256sum --check --ignore-missing node-SHASUMS256.txt)
tar -xJf "$FROST_WORK/node-v22.23.5-linux-x64.tar.xz" \
  -C "$FROST_WORK/node" --strip-components=1
export PATH="$FROST_WORK/node/bin:$PATH"
node --version
```

## 3. Fetch the pinned platform

```sh
python tools/fetch_platform.py --destination "$FROST_WORK/OpenGP5"
python tools/fetch_platform.py --component media --destination "$FROST_WORK/OpenGP5Media"
```

The commit and repository URL come from `dependencies.lock.json`. Do not
replace the platform with an arbitrary latest checkout: Frost's staging
patches target the locked source. The helper refuses to overwrite an existing
platform directory. Reuse a previously fetched matching checkout when present.

## 4. Download and install both compilers

```sh
python tools/fetch_pinned_toolchains.py --output "$FROST_WORK/compiler-archives"
bash tools/setup_toolchains.sh "$FROST_WORK/compiler-archives" "$FROST_WORK/toolchains"
```

The download tool verifies SHA-256 values before extraction. The setup script
installs Arm GNU 14.2.Rel1 for the DSP and Andes NDS32 v3s for the controller.
Do not commit either compiler or its archive to this repository.

## 5. Run host checks

```sh
CC=clang PYTHONDONTWRITEBYTECODE=1 python -m unittest discover -s tests -v
cmake -S . -B "$FROST_WORK/host" -DCMAKE_C_COMPILER=clang
cmake --build "$FROST_WORK/host" --parallel 2
```

Host tests compile native fixtures; some require Node.js. Optional checks
that need historical firmware images are skipped in a clean checkout.
These checks do not measure the physical pedal's timing or audio quality.
Some fixtures write into `build/`; archive that output after testing:

```sh
python tools/clean_build.py --archive "$FROST_WORK/host-test-output"
```

Use a fresh archive destination for each run. Set `CC` to your compiler path
if automatic discovery fails. On Windows, native Python can use Clang from
Visual Studio's LLVM tools for these checks.

## 6. Build the release firmware with USB audio and BLE MIDI

This profile uses the pedal input and enables audio output. It includes the
controller IR link, factory preset initialization, USB audio and BLE MIDI.
This is the single distributed firmware configuration:

```sh
python tools/prepare_gp5.py \
  --platform "$FROST_WORK/OpenGP5" \
  --media-platform "$FROST_WORK/OpenGP5Media" \
  --stage "$FROST_WORK/gp5-release" \
  --audio-input adc --audio-output line-test \
  --control-scheduler --large-code --xflash --overlays \
  --reverb-int16 --dsp-usage --ir-link
bash tools/build_gp5.sh "$FROST_WORK/gp5-release" "$FROST_WORK/toolchains"
```

Output: `$FROST_WORK/gp5-release/build/GP5_Frost_line_test.bin`.
Despite its development filename, this is the audio-capable profile.
Use the full options shown above: the historical bare-default diagnostic
configuration does not fit the current complete effect library in flash.
Do not enlarge memory regions to bypass a linker overflow.

Without a supplied cabinet, source builds use the synthetic diagnostic FIR
in `tests/fixtures/diagnostic_ir.h`. Release images embed the CC0 "Cookie
Monster" cabinet instead (see THIRD_PARTY_NOTICES.md), so the result will not
sound identical. To match a release, download `01-cookie-monster.wav` from
[OrbitCab](https://github.com/darwinscat/orbitcab/tree/main/resources/ir), or
bake your own cabinet, with these staging options:

```sh
--cab-ir /absolute/path/outside/repository/cabinet.wav --cab-ir-taps 512
```

The 512-tap option requires the compact-reverb profile shown above. Use only
assets you are entitled to use/share. No private capture, cabinet, generated
model bank or proprietary stock firmware is needed for a source build.

Each stage directory must be new. Staging copies the locked platform and
applies Frost adaptations to that copy; it does not patch your source checkout
or flash the pedal.

## 7. Check and identify the output

```sh
python "$FROST_WORK/gp5-release/tools/gp5tool.py" check \
  "$FROST_WORK/gp5-release/build/GP5_Frost_line_test.bin"
sha256sum "$FROST_WORK/gp5-release/build/GP5_Frost_line_test.bin"
```

The build script also checks the package and prints DSP/controller sizes.
Review failures instead of ignoring memory overflows. Keep the stage's
`frost-input.json` and `build/audit/dsp.elf` alongside the image outside source.
The playable image contains DSP, DSP-data and controller app sections; it
does not replace user asset/preset storage.

To verify the actual release file through the website flasher simulation:

```sh
FROST_TEST_FIRMWARE="$FROST_WORK/gp5-release/build/GP5_Frost_line_test.bin" \
  PYTHONDONTWRITEBYTECODE=1 python -m unittest discover -s tests -p test_web_flasher.py -v
```

This checks file identity, corrupt-file rejection and transfer of every section
into a simulated pedal. It does not access or flash a physical device.

Check `firmware/version.h` for the canonical semantic version and uppercase
display spelling. Custom builds have their own checksum, even if they retain
the same version text. A checksum identifies bytes; it is not a signature.

### USB audio and BLE MIDI integration

The **OpenGP5Media** source pin supplies OpenGP5 1.0's USB audio, resampler
and BLE MIDI drivers. Section 3 fetches both pins and section 6 includes
`--media-platform` in the release build. Omitting that option creates a
diagnostic MIDI-only build; it is not a release package.

The two processors share **one** SPI transport: 167 Frost IR/control words
plus 64 packed stereo USB frames (231 words total). The controller runs the
upstream USB resampler after its IR work. The DSP records after the stereo
effects/EQ, before playback mixing and the listening master. Separate recording
and playback staging buffers preserve the early IR exchange timing; USB
recording gains one block (64 samples) of buffering. Bypassed IRs (including
custom IR uploads), silent route-commit blocks and empty chains start their zero-stream exchange
before chain processing, leaving the controller its usual processing window.
Coefficient mirroring pauses while the receive/validation/expansion stages use
the coefficient area as scratch, then resumes with the validated cabinet.
Quick recall negotiates a pre-fade handshake with the DSP. The controller
waits for two fully silent blocks before sending the full preset; commit
and two-block warm-up run under a final stereo envelope after bypass/EQ.
Older peers retain their existing recall path. This is a transition strategy,
not physical audio-continuity qualification.
DSP media code
uses the existing extended code region; firmware memory limits are unchanged.
Both IR streams remain available. Bluetooth starts before USB attachment and
uses the existing MIDI handler with separate USB/BLE SysEx parser storage.

Short device checks have exercised USB recording/playback with a BLE MIDI
connection and IR processing, and received MIDI messages over BLE. USB audio/BLE
remains experimental: long-session audio continuity and heavy-chain headroom
are not qualified. Host tests do not measure physical real-time load. It supports BLE MIDI, not
Bluetooth audio streaming. The release command in section 6 enables both features.

Native integration tests use `FROST_MEDIA_PLATFORM=/path/to/OpenGP5Media` in
the environment; without the checkout those integration tests skip.
Upstream's `platform/nds32/test/usb_audio_test.sh` checks resampling and clock
drift with modeled IO. A physical read-only status check after an authorized
flash is:

```sh
python tools/probe_media.py --stage "$FROST_WORK/gp5-media"
```

That command reports USB streaming flags, playback/recording buffer counters,
clock correction, BLE state, and MIDI queue counters. Optional `--direct off`
or `--direct on` changes volatile monitoring. Frost uses **7E 21** for this
operation: upstream's **7E 19** is already Frost's live-slot query. The USB
name stays **GP-5 Open FW** for existing Frost tools, with a new audio-profile
serial so Windows re-reads the composite descriptors; BLE advertises **Frost**.
When upgrading from an older MIDI-only build, install the complete image
(DSP, DSP-data and controller app); their shared SPI frame length differs.
No source build flashes anything by itself.

## 8. Install only when intended

Building does not install anything. Follow the [Installation Guide](Installation%20Guide.MD)
to use the browser installer. For an explicitly authorized development flash,
the staged command-line tool also supports:

```sh
python -m pip install mido python-rtmidi
python "$FROST_WORK/gp5-release/tools/gp5tool.py" flash \
  "$FROST_WORK/gp5-release/build/GP5_Frost_line_test.bin" \
  --sections dsp,dsp-data,app
```

On Windows, normally run USB-MIDI tools in native Windows Python with Windows
paths to the same stage; WSL does not automatically expose the USB-MIDI device.
This flash command is a device write. Check the exact file, backups and recovery
procedure first, and do not use it merely to validate a source change.

## 9. Website and developer structure

For a complete C effect example and registration steps, see
[Build an effect](Build%20effect.MD).

To serve the static website locally, run from the repository root:

```sh
python -m http.server 8080 --directory web
```

Open `http://localhost:8080` in Chrome/Edge. Remote hosting needs HTTPS for
browser device access. The Pages workflow is manually triggered; it publishes
a staged copy of `web/` plus the verified release firmware selected by tag.
It does not publish captures, compiler files or research output.

| Code | Responsibility |
|---|---|
| engine/chain.c, effect.c, preset.c | Processing order, instance registry, transitions and preset state |
| effects/<name>/ | DSP kernels and effect-specific state |
| firmware/controller/ | PARA/footswitch, drawing, persistent storage and DSP clients |
| firmware/dsp/ | Audio conversion, bounded servicing, transport, timing and fault guards |
| firmware/protocol/ | Shared command IDs, wire formats and request validation |
| web/ | Live editor, model/IR tools, setlist management and updater |
| tests/ | Native C fixtures and Python/JavaScript regression checks |

Keep protocol IDs stable. Derive resource constraints from the linked build,
then measure real input and transitions on hardware. Do not allocate memory,
block on IO or write flash in an audio callback. The shipping capture backend is in `effects/fcm/`; native NAM research is
not part of this product source tree.

For release packaging, `tools/package_beta.py --help` describes local ZIP
creation. The default branding mode expects an accepted baseline and verifies
unchanged DSP sections. Use `--candidate --notes /external/RELEASE_NOTES.md`
for a build with changed DSP code that still needs qualification; its manifest
records the actual changed sections and marks hardware qualification pending.
Use a baseline with USB audio/BLE enabled. Packaging rejects MIDI-only builds. The package
includes all seven guides, their linked public images, notices and checksums.

For example, after building a fresh external stage:

```sh
python tools/package_beta.py "$FROST_WORK/gp5-release" \
  --baseline "$FROST_WORK/previous-release.bin" \
  --out "$FROST_WORK/release" --candidate \
  --notes "$FROST_WORK/RELEASE_NOTES.md"
```

Firmware stays separate from website hosting. Packaging does not publish,
deploy, flash, establish audible continuity or approve a stable release.

## 10. Publish the initial preview and deploy GitHub Pages

The release has one firmware file, `Frost_GP5_0.1.0.bin`, containing
USB audio, USB MIDI, BLE MIDI and all effect/preset/IR changes. The matching
ZIP contains this same image plus the seven guides and public images.
This is the initial preview release. USB audio/BLE and audible preset-switching
qualification remain open;
include the supplied release notes and mark the GitHub release as a prerelease.

1. Commit the release preparation on `main`, review it, and push `main` to
   `origin`. Wait for **Actions → Host checks** to pass. Keep binaries,
   captures and compiler output outside the source repository.
2. Open the repository's **Releases → Draft a new release**. Choose a new tag
   **v0.1.0**, targeting the release commit on `main`. Use the title
   **Frost 0.1.0**, paste the supplied `RELEASE_NOTES.md`, and select
   **Set as a pre-release**. Attach the `.bin`, matching `.zip`,
   `SHA256SUMS.txt` and `manifest.json` from the single release folder.
   Publish after reviewing the notes and remaining physical checks.
3. In **Settings → Pages → Build and deployment**, set **Source** to
   **GitHub Actions**. For a public site, check the repository visibility and
   Pages availability for your account. Do not choose `/docs`: that folder
   contains Markdown guides, not the browser application.
4. Open **Actions → Web tools (GitHub Pages) → Run workflow**. Choose branch
   **main**, enter **v0.1.0** in **release_tag**, and run it. The tag
   must match the firmware version on this branch. Approve the `github-pages`
   environment if your repository requires it.
5. The workflow runs source/converter/pedal-file checks, downloads only the
   named `.bin` and `SHA256SUMS.txt` from that release, verifies the firmware
   identity/version/sections, CRC and SHA-256, and tests simulated transfer.
   It stages a copy of `web/` with that verified image and a matching download
   manifest. No firmware is committed into Git.
6. Wait for the deployment job and open its **github-pages** environment URL.
   For this repository the expected default URL is
   [Frost Studio](https://freelender.github.io/frost-gp5/). Test the demo,
   confirm **Firmware → Install Frost** shows the intended version, then
   test USB connection and the intended explicitly authorized installation.
   A successful Pages deployment does not qualify physical audio behavior.

A blank **release_tag** deploys the editor without bundled firmware; users
can still select a local `.bin`. A missing release asset, bad checksum or
version mismatch fails the workflow before upload/deployment. Do not weaken
the checks to force a deployment: correct the release/tag/assets and rerun.
Later website-only updates can deploy against the same matching release tag.

If **Run workflow** is missing, push the workflow file to the default branch
and check that Actions are enabled. A `github-pages` approval can hold the job
until an authorized reviewer approves it. For a 404, wait for the successful
deployment and use its environment link; the project path `/frost-gp5/` is part
of the URL. Cloudflare deployment is independent of GitHub Pages.

GitHub documents [release creation](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)
and [Pages with a custom workflow](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).
These steps guide an intended publication; local packaging never publishes
or deploys automatically.

## 11. Leave a clean checkout

Keep logs, screenshots, compiler files, private assets and one-off scripts in
`FROST_WORK`. Do not add development diaries or reports to docs/.

```sh
python tools/check_repository.py --require-empty-build
```

The source tree's `build/` folder must be empty at handoff. Source pushes,
host tests and emulator runs do not constitute a physical firmware qualification
or authorize deployment.
