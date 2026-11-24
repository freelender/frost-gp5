# Source acknowledgments

Frost's hardware adapter follows [OpenGP5](https://github.com/OpenGP5/OpenGP5) interfaces/examples. The pinned platform is recorded in `dependencies.lock.json`. Copied API headers under `tests/stubs/` and controller/display adaptations retain the OpenGP5 MIT notice below. The dependency retains its own license. The controller API test header was refreshed to OpenGP5 ce8417c (0.2.1-alpha). The IR link (`firmware/controller/ir_link.h`, `firmware/dsp/ir_link.h`, `tools/platform_compat.py patch_ir_link`) ports OpenGP5 0.5.5-alpha (7868e6d) audio-link code: its SPI-slave DMA driver, exchange handler, DSP SPI 1 framing/setup and loader hand-over behavior, under the same MIT notice.

Native NAM research kernels, archived separately from the product source, were locally implemented during research using NAM-to-Zoom and NeuralAmpModelerCore as references. Official NAM Core/Eigen/AudioDSPTools are not linked or vendored into this standalone host/embedded build. The broader research workspace retains its source pins and independent reference tests.

Integer arithmetic in the separately archived native quantized research derives from the Thomas Hammer MIT nam-core 0.15.6 reference inspected during research, with locally authored M4 kernels, delay retiming and rate conversion. It is not the original Zoom kernel. The original JavaScript bundle, converters/calibration and private generated capture banks are not distributed in this snapshot. The MIT notices below apply to those adaptations.

The default hardware diagnostic uses a locally generated synthetic FIR, not a cabinet capture. No trained NAM weights, stock firmware or user IR assets are bundled. Historical migration records are retained outside the product source tree.

## Built-in cabinet impulse response

Release firmware images embed one cabinet impulse response as the built-in cabinet (CAB 00): **"Cookie Monster"** (`01-cookie-monster.wav`) from the **Jesters Brutal Pack 1.0** by Jesters, released under **CC0 1.0** (public domain dedication). Frost obtained it from [OrbitCab](https://github.com/darwinscat/orbitcab) by Darwin's Cat (`resources/ir/`), whose [asset ledger](https://github.com/darwinscat/orbitcab/blob/main/docs/ASSET-LICENSES.md) records the pack's CC0 grant. For the firmware it is trimmed to 512 taps and stored as 16-bit coefficients. CC0 requires no attribution; this credit is given with thanks. The WAV itself is not part of this source tree.

MIT License

Copyright (c) 2026 the OpenGP5 contributors

Copyright (c) 2026 Aleksandar Vukasinovic

Copyright (c) 2026 Thomas Hammer

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
