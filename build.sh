#!/bin/bash

# Simple build script for Order Matching Engine

set -e  # Exit on error

echo "=== Order Matching Engine Build Script ==="
echo ""

# Clean previous build
if [ -d "build" ]; then
    echo "Cleaning previous build..."
    rm -rf build
fi

# Configure
echo "Configuring with CMake..."
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
echo ""
echo "Building..."
cmake --build build -j$(nproc)

# Test
echo ""
echo "Running tests..."
cd build && ctest --output-on-failure

echo ""
echo "=== Build Complete ==="
echo "Run the main executable: ./build/ome_main"
echo "Run tests: cd build && ctest"