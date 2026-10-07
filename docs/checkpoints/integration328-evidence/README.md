# Historical integration evidence

Measured source: 8762774a9e521f127c9e331adb4a23e567d74ba2 for the tyre
law comparison; 64a2a91d0cf4ca830bd34cc9ce527bafa385c2f0 for the recovered
steering matrix. These reports are history, not current test pins.

`tyre_native.py` executes authenticated original 0x00587740 and its bounded
original math helpers through the tracked physics256_native Machine. Inputs
are synthetic wheel pointers, coefficients, load in kN and slip in radians.
Its return capture is an explicit emulator-only stub. No original math or
force function is replaced. The emulator's synthetic x87 control word is
0x007F; this does not establish the live game's floating-point environment.

Run from the repository root with standalone Python, Unicorn and Capstone:

```text
python -B docs/checkpoints/integration328-evidence/tyre_native.py --root RETAIL --out build/tyre-native-fresh
```

`tyre_formula.cpp` includes the actual vehicle.cpp curve and keeps the
selection expression measured at the recorded head. Compile from the root:

```text
g++ -std=c++17 -O2 -ffp-contract=off -Iinclude docs/checkpoints/integration328-evidence/tyre_formula.cpp src/sha256.cpp -o build/tyre-formula.exe
```

Its input columns are slip radians, load N, slip_scale, load_scale and
force_scale; output is force N. `tyre_comparison.json` records the native
inputs and original kN-to-N comparisons against that compiled expression.
The source SHA is the raw file byte hash, including line endings. This
validates the isolated lateral law and units, not live wheel/engine feedback.
The relocated script and formula were smoke-tested from the real workspace.

`recovered_matrix.cpp` mirrors the current RaceSession steering context on
flat ground, using actual steering_step and VehicleModel. Compile with
include and build/generated, linking the existing build libraries. It
records initial/pedal inputs, actual speed, sideslip, body orientation,
wheel support, and the result after releasing the stick. The CSV is the
recorded historical measurement. A sideslip threshold is not a spin verdict;
coasting/braking samples that have stopped do not prove moving recovery.
Full original engine feedback and console controllability remain open.
