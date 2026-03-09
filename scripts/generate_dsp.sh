#!/bin/bash
set -e

cd "$(dirname "$0")/.."

echo "=== Generating C++ from Faust DSP ==="

# -vec enables automatic vectorization (SIMD/auto-vectorization-friendly loops)
# -vs 32 sets the vector size to 32 samples (good balance for plugin buffers)
faust -cn FirstAirDSP -lang cpp -i \
    -vec -vs 32 \
    dsp/firstair.dsp \
    -o generated/FirstAirDSP.h

echo "=== Generated: generated/FirstAirDSP.h ==="
echo "    $(wc -l < generated/FirstAirDSP.h) lines"
