#!/bin/bash
set -e

echo "=== Installing First Air dependencies ==="

# Homebrew packages
brew install faust cmake

# Git submodules
cd "$(dirname "$0")/.."
git submodule update --init --recursive

echo "=== Dependencies installed ==="
echo "Faust: $(faust --version | head -1)"
echo "CMake: $(cmake --version | head -1)"
