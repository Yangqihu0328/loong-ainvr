# ============================================================
# Loong AI NVR — Makefile Wrapper
# ============================================================
# Convenience targets wrapping CMake presets and common tasks.
# Usage:
#   make build          — Debug build (default)
#   make release        — Release build
#   make test           — Run all tests
#   make format         — Format C++ sources (clang-format)
#   make format-check   — Check C++ formatting (dry-run)
#   make lint           — Run clang-tidy static analysis
#   make coverage       — Build with coverage + generate report
#   make docker         — Build Docker image
#   make clean          — Remove build directories
# ============================================================

.PHONY: build release test format format-check lint coverage docker clean help \
        configure-debug configure-release configure-ci web package run run-release

NPROC ?= $(shell nproc 2>/dev/null || echo 4)
BUILD_DIR_DEBUG   = build/debug
BUILD_DIR_RELEASE = build/release
BUILD_DIR_CI      = build/ci
BUILD_DIR_COV     = build/coverage

# ── Default target ──
all: build

# ── Configure ──
configure-debug:
	cmake --preset debug

configure-release:
	cmake --preset release

configure-ci:
	cmake --preset ci

# ── Build ──
build: configure-debug
	cmake --build --preset debug

release: configure-release
	cmake --build --preset release

# ── Test ──
test: build
	ctest --preset debug

# ── Format (clang-format) ──
SRC_FILES = $(shell find src tests -name '*.cc' -o -name '*.h' 2>/dev/null)

format:
	@echo "Formatting $(words $(SRC_FILES)) files..."
	@clang-format -i $(SRC_FILES)
	@echo "Done."

format-check:
	@echo "Checking formatting..."
	@clang-format --dry-run --Werror $(SRC_FILES)
	@echo "All files formatted correctly."

# ── Lint (clang-tidy) ──
lint: configure-ci
	cmake --build --preset ci

# ── Coverage ──
coverage:
	cmake --preset coverage
	cmake --build --preset coverage
	ctest --preset coverage
	@echo "Generating coverage report..."
	@lcov --capture --directory $(BUILD_DIR_COV) --output-file $(BUILD_DIR_COV)/coverage.info \
		--ignore-errors mismatch 2>/dev/null || true
	@lcov --remove $(BUILD_DIR_COV)/coverage.info '/usr/*' '*/third_party/*' '*/tests/*' \
		--output-file $(BUILD_DIR_COV)/coverage_filtered.info 2>/dev/null || true
	@genhtml $(BUILD_DIR_COV)/coverage_filtered.info --output-directory $(BUILD_DIR_COV)/html 2>/dev/null || true
	@echo "Coverage report: $(BUILD_DIR_COV)/html/index.html"

# ── Frontend ──
web:
	cd web && npm ci && npm run build

# ── Package (assemble runtime in build/bin/) ──
package: build web
	bash scripts/setup-runtime.sh debug

package-release: release web
	bash scripts/setup-runtime.sh release

# ── Run (build + package + start server) ──
run: package
	cd build/bin && ./loong-ainvr config.json

run-release: package-release
	cd build/bin && ./loong-ainvr config.json

# ── Docker ──
docker:
	docker build -t loong-ainvr:latest .

docker-gpu:
	docker compose -f docker-compose.gpu.yml build

# ── Clean ──
clean:
	rm -rf build/debug build/release build/ci build/coverage build/sanitizer

# ── Help ──
help:
	@echo "Loong AI NVR — Available targets:"
	@echo "  make build          Debug build (default)"
	@echo "  make release        Release build"
	@echo "  make test           Run unit tests"
	@echo "  make package        Build + assemble runtime in build/bin/"
	@echo "  make run            Build + package + start server (debug)"
	@echo "  make run-release    Build + package + start server (release)"
	@echo "  make format         Format C++ sources"
	@echo "  make format-check   Check C++ formatting"
	@echo "  make lint           clang-tidy static analysis"
	@echo "  make coverage       Build + coverage report"
	@echo "  make web            Build frontend (npm)"
	@echo "  make docker         Build Docker image"
	@echo "  make docker-gpu     Build Docker image (GPU)"
	@echo "  make clean          Remove build dirs"
