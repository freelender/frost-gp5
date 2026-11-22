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

## 6. Build the playable ADC/IR-link beta profile

This profile uses the pedal input and enables audio output. It includes the
controller IR link and factory preset initialization:

```sh
python tools/prepare_gp5.py \
  --platform "$FROST_WORK/OpenGP5" \
  --stage "$FROST_WORK/gp5-beta" \
  --audio-input adc --audio-output line-test \
  --control-scheduler --large-code --xflash --overlays \
  --reverb-int16 --dsp-usage --ir-link
bash tools/build_gp5.sh "$FROST_WORK/gp5-beta" "$FROST_WORK/toolchains"
```

Output: `$FROST_WORK/gp5-beta/build/GP5_Frost_line_test.bin`.
Despite its development filename, this is the audio-capable profile.
Use the full options shown above: the historical bare-default diagnostic
configuration does not fit the current complete effect library in flash.
Do not enlarge memory regions to bypass a linker overflow.

Without a supplied cabinet, source builds use the synthetic diagnostic FIR
in `tests/fixtures/diagnostic_ir.h`. This is not the private cabinet embedded
in the supplied beta image, and the result will not sound identical. To bake
your own cabinet, add these staging options before running the build:

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
python "$FROST_WORK/gp5-beta/tools/gp5tool.py" check \
  "$FROST_WORK/gp5-beta/build/GP5_Frost_line_test.bin"
sha256sum "$FROST_WORK/gp5-beta/build/GP5_Frost_line_test.bin"
```

The build script also checks the package and prints DSP/controller sizes.
Review failures instead of ignoring memory overflows. Keep the stage's
`frost-input.json` and `build/audit/dsp.elf` alongside the image outside source.
The playable image contains DSP, DSP-data and controller app sections; it
does not replace user asset/preset storage.

Check `firmware/version.h` for the canonical semantic version and uppercase
display spelling. Custom builds have their own checksum, even if they retain
the same version text. A checksum identifies bytes; it is not a signature.

## 8. Install only when intended

Building does not install anything. Follow the [Installation Guide](Installation%20Guide.MD)
to use the browser installer. For an explicitly authorized development flash,
the staged command-line tool also supports:

```sh
python -m pip install mido python-rtmidi
python "$FROST_WORK/gp5-beta/tools/gp5tool.py" flash \
  "$FROST_WORK/gp5-beta/build/GP5_Frost_line_test.bin" \
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
only `web/`, not firmware or private files.

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
creation. It expects an accepted baseline and verifies unchanged DSP sections
for a version/packaging-only release. Firmware stays separate from website
hosting. A changed DSP build needs its own validation and accepted baseline.

## 10. Leave a clean checkout

Keep logs, screenshots, compiler files, private assets and one-off scripts in
`FROST_WORK`. Do not add development diaries or reports to docs/.

```sh
python tools/check_repository.py --require-empty-build
```

The source tree's `build/` folder must be empty at handoff. Source pushes,
host tests and emulator runs do not constitute a physical firmware qualification
or authorize deployment.
