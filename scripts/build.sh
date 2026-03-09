#!/bin/bash
set -e

cd "$(dirname "$0")/.."

# Step 1: Generate C++ from Faust
bash scripts/generate_dsp.sh

# Step 2: CMake configure (if needed)
if [ ! -f build/CMakeCache.txt ]; then
    echo "=== Configuring CMake ==="
    cmake -B build -DCMAKE_BUILD_TYPE=Release
fi

# Step 3: Build
echo "=== Building ==="
cmake --build build --config Release -j$(sysctl -n hw.ncpu)

echo ""
echo "=== Build complete ==="
echo "VST3: $(find build -name '*.vst3' -type d 2>/dev/null | head -1)"
echo "AU:   $(find build -name '*.component' -type d 2>/dev/null | head -1)"
echo "CLAP: $(find build -name '*.clap' 2>/dev/null | head -1)"
