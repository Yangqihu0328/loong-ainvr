#!/bin/bash
# Loong AI NVR - Development Dependencies Installation Script
# Target: Ubuntu 22.04 LTS (x86_64)

set -e

echo "=== Loong AI NVR: Installing Development Dependencies ==="
echo ""

# Update package list
sudo apt update

# Build essentials
echo "[1/6] Installing build tools..."
sudo apt install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  git \
  clang-format-14 \
  clang-tidy-14

# Create symlinks for clang tools
sudo update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-14 100 2>/dev/null || true
sudo update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-14 100 2>/dev/null || true

# FFmpeg development libraries
echo "[2/6] Installing FFmpeg dev libraries..."
sudo apt install -y \
  libavcodec-dev \
  libavformat-dev \
  libavutil-dev \
  libswscale-dev \
  libswresample-dev \
  ffmpeg

# OpenCV
echo "[3/6] Installing OpenCV..."
sudo apt install -y libopencv-dev

# C++ libraries
echo "[4/6] Installing C++ libraries..."
sudo apt install -y \
  libspdlog-dev \
  nlohmann-json3-dev \
  libsqlite3-dev \
  sqlite3 \
  libssl-dev \
  libcurl4-openssl-dev \
  libwebsocketpp-dev \
  libbenchmark-dev

# Testing libraries
echo "[5/6] Installing testing libraries..."
sudo apt install -y \
  libgtest-dev \
  libgmock-dev

# Node.js (for Web UI development)
echo "[6/6] Installing Node.js..."
if ! command -v node &> /dev/null; then
  curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
  sudo apt install -y nodejs
fi

echo ""
echo "=== Dependency Installation Complete ==="
echo ""

# Verify installations
echo "=== Verification ==="
echo "  g++:          $(g++ --version 2>&1 | head -1)"
echo "  cmake:        $(cmake --version 2>&1 | head -1)"
echo "  ninja:        $(ninja --version 2>&1)"
echo "  ffmpeg:       $(ffmpeg -version 2>&1 | head -1)"
echo "  opencv:       $(pkg-config --modversion opencv4 2>&1)"
echo "  sqlite3:      $(sqlite3 --version 2>&1)"
echo "  node:         $(node --version 2>&1)"
echo "  npm:          $(npm --version 2>&1)"
echo ""
echo "=== Ready to build! Run: ==="
echo "  mkdir build && cd build"
echo "  cmake -G Ninja .."
echo "  ninja"
echo "  ctest --output-on-failure"
