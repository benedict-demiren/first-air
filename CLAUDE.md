# First Air — Project Conventions

## Project Overview
Physics-based reverb VST3/AU/CLAP plugin. DSP written in Faust, compiled to C++, wrapped in CMake + JUCE.

## Build Pipeline
1. Edit DSP: `dsp/firstair.dsp`
2. Generate C++: `bash scripts/generate_dsp.sh` (runs `faust -cn FirstAirDSP -lang cpp -i`)
3. Build all formats: `cmake --build build --config Release -j$(sysctl -n hw.ncpu)`
4. Full rebuild: `bash scripts/build.sh`

If CMakeLists.txt changes, delete `build/` and re-run `cmake -B build -DCMAKE_BUILD_TYPE=Release`.

## Architecture
- `dsp/firstair.dsp` — **The core DSP file.** All audio processing lives here.
- `generated/FirstAirDSP.h` — Auto-generated from Faust. Never edit directly.
- `Source/PluginProcessor.cpp` — JUCE wrapper. Bridges Faust parameters to JUCE APVTS.
- `Source/PluginEditor.cpp` — Auto-generated parameter UI (sliders + combo boxes).
- `CMakeLists.txt` — Build config. Targets: VST3, AU, CLAP, Standalone.

## Faust Conventions
- Parameter paths use slashes: `[0]Space/[1]Width`
- `si.smoo` for parameter smoothing (already on all params)
- To keep unused params alive: use `attach(_, param)` or route through bargraphs
- Max delay sizes must be compile-time constants; actual delay lengths are runtime
- Use `ba.selectn(N, idx)` for material coefficient lookup

## Key Dependencies
- Faust 2.83.1+ (brew install faust)
- CMake 4.x (brew install cmake)
- JUCE (git submodule at libs/JUCE)
- clap-juce-extensions (git submodule at libs/clap-juce-extensions)
- Faust headers at /opt/homebrew/include/faust/

## CMake Notes
- Use `LANGUAGES C CXX` (not just CXX) — JUCE needs C
- Version range `3.22...4.2` for CMake 4.x compatibility
- Plugins auto-copied to ~/Library/Audio/Plug-Ins/ after build
