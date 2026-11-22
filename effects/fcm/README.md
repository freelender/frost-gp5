# Frost Capture Model

The NAM-labelled capture block plays FCM approximations of supported NAM A2
captures. It does not run native NAM. Browser conversion lives in
web/converter/; record packing/validation is mirrored in tools/fcm_bank.py.

| Source | Purpose |
|---|---|
| fcm_effect.* | Runtime block, model selection and transfer integration |
| fcm_iir.* | IIR-based capture model processing |
| fcm.* | Direct-form reference implementation |
| fcm_fast.* | FIR/FFT reference and optimization implementation |

Model records carry their own coefficients and integrity checks. No private
generated model bank belongs in this directory. The built-in fallback and
synthetic test models are distinct from a user's converted capture.

For user workflows see [User Guide](../../docs/User%20Guide.md). For building
and code organization see [Build Firmware](../../docs/Build%20Firmware.md).
