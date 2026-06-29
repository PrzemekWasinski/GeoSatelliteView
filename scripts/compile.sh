#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "Installing dependencies..."
sudo apt-get update -qq
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libopencv-dev \
    libcurl4-openssl-dev

echo "Compiling..."
cmake -S "$PROJECT_ROOT" -B "$PROJECT_ROOT/build"
cmake --build "$PROJECT_ROOT/build" -j$(nproc)

echo "Compiled successfully! Run with: ./scripts/run.sh"
