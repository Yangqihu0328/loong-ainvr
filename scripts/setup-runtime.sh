#!/usr/bin/env bash
# =============================================================================
# Loong AI NVR — Local Runtime Environment Setup
# =============================================================================
# Assembles a complete runtime directory from build artifacts + project files.
#
# Usage:
#   bash scripts/setup-runtime.sh [BUILD_TYPE]
#
#   BUILD_TYPE: debug (default) | release
#
# Output:  build/bin/
#   ├── loong-ainvr          # executable
#   ├── config.json          # local config (relative paths)
#   ├── web/                 # frontend dist (if built)
#   ├── models/              # model files (.onnx)
#   ├── plugins/             # AI model plugins (.so)
#   ├── recordings/          # recording storage
#   └── data/                # SQLite databases (users/rules/index)
# =============================================================================

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_TYPE="${1:-debug}"

case "$BUILD_TYPE" in
  debug)   BUILD_DIR="$PROJECT_ROOT/build/debug" ;;
  release) BUILD_DIR="$PROJECT_ROOT/build/release" ;;
  *)
    if [ -d "$PROJECT_ROOT/build/$BUILD_TYPE" ]; then
      BUILD_DIR="$PROJECT_ROOT/build/$BUILD_TYPE"
    elif [ -f "$PROJECT_ROOT/build/loong-ainvr" ]; then
      BUILD_DIR="$PROJECT_ROOT/build"
    else
      echo "Error: build directory not found for type '$BUILD_TYPE'"
      echo "Available: debug, release, or specify custom build dir name"
      exit 1
    fi
    ;;
esac

RUNTIME_DIR="$PROJECT_ROOT/build/bin"
EXECUTABLE="$BUILD_DIR/loong-ainvr"

if [ ! -f "$EXECUTABLE" ]; then
  echo "Error: executable not found at $EXECUTABLE"
  echo "Run 'make build' or 'make release' first."
  exit 1
fi

echo "=== Loong AI NVR — Setting up runtime environment ==="
echo "  Source:  $BUILD_DIR"
echo "  Target:  $RUNTIME_DIR"
echo ""

# Create directory structure
mkdir -p "$RUNTIME_DIR"/{models,plugins,recordings,data,web}

# Copy executable
cp -f "$EXECUTABLE" "$RUNTIME_DIR/loong-ainvr"
chmod +x "$RUNTIME_DIR/loong-ainvr"
echo "[OK] Executable: loong-ainvr"

# Copy local config
cp -f "$PROJECT_ROOT/config/local.json" "$RUNTIME_DIR/config.json"
echo "[OK] Config: config.json"

# Copy frontend dist (if available)
# vite.config.js outputs to PROJECT_ROOT/dist/web/
if [ -d "$PROJECT_ROOT/dist/web" ]; then
  cp -rf "$PROJECT_ROOT/dist/web/"* "$RUNTIME_DIR/web/" 2>/dev/null || true
  echo "[OK] Frontend: web/ (from dist/web/)"
elif [ -d "$PROJECT_ROOT/web/dist" ]; then
  cp -rf "$PROJECT_ROOT/web/dist/"* "$RUNTIME_DIR/web/" 2>/dev/null || true
  echo "[OK] Frontend: web/ (from web/dist/)"
else
  echo "[--] Frontend: skipped (run 'cd web && npm run build' first)"
fi

# Copy models (if any exist)
MODEL_COUNT=0
if [ -d "$PROJECT_ROOT/models" ]; then
  MODEL_COUNT=$(find "$PROJECT_ROOT/models" -name '*.onnx' -o -name '*.engine' 2>/dev/null | wc -l)
  if [ "$MODEL_COUNT" -gt 0 ]; then
    cp -f "$PROJECT_ROOT/models/"*.{onnx,engine} "$RUNTIME_DIR/models/" 2>/dev/null || true
  fi
fi
echo "[OK] Models: $MODEL_COUNT files"

# Copy plugins (if any exist)
PLUGIN_COUNT=0
if [ -d "$PROJECT_ROOT/plugins" ]; then
  PLUGIN_COUNT=$(find "$PROJECT_ROOT/plugins" -name '*.so' 2>/dev/null | wc -l)
  if [ "$PLUGIN_COUNT" -gt 0 ]; then
    cp -f "$PROJECT_ROOT/plugins/"*.so "$RUNTIME_DIR/plugins/" 2>/dev/null || true
  fi
fi
echo "[OK] Plugins: $PLUGIN_COUNT files"

echo ""
echo "=== Runtime environment ready ==="
echo ""
echo "  $RUNTIME_DIR/"
echo "  ├── loong-ainvr          # executable"
echo "  ├── config.json          # configuration"
echo "  ├── web/                 # frontend assets"
echo "  ├── models/              # AI models (.onnx)"
echo "  ├── plugins/             # model plugins (.so)"
echo "  ├── recordings/          # video recordings"
echo "  └── data/                # databases (auto-created)"
echo ""
echo "To run:"
echo "  cd $RUNTIME_DIR && ./loong-ainvr config.json"
echo ""
