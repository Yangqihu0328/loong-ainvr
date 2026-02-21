cmake_minimum_required(VERSION 3.20)

project(loong-ainvr
  VERSION 0.3.0
  DESCRIPTION "Loong AI NVR - Embedded Intelligent Video Analysis System"
  LANGUAGES CXX
)

# ============================================================
# C++ Standard & Compiler Settings (Google C++ Style)
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict warning flags (Google C++ Style compliance)
add_compile_options(
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -Wno-unused-parameter
  -Wshadow
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Wcast-align
  -Woverloaded-virtual
  -Wconversion
  -Wsign-conversion
)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# Debug flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# ============================================================
# Options
# ============================================================
option(LOONG_BUILD_TESTS "Build unit tests" ON)
option(LOONG_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(LOONG_ENABLE_ONNXRUNTIME "Enable ONNX Runtime backend" OFF)
option(LOONG_ENABLE_ONNXRUNTIME_CUDA "Enable ONNX Runtime CUDA EP" OFF)
option(LOONG_ENABLE_TENSORRT "Enable TensorRT backend" OFF)
option(LOONG_ENABLE_SANITIZERS "Enable address/UB sanitizers" OFF)
option(LOONG_ENABLE_CLANG_TIDY "Enable clang-tidy static analysis" OFF)
option(LOONG_ENABLE_COVERAGE "Enable code coverage (gcov/lcov)" OFF)

# ── clang-tidy integration ──
if(LOONG_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE
    NAMES clang-tidy clang-tidy-18 clang-tidy-17 clang-tidy-16
          clang-tidy-15 clang-tidy-14
  )
  if(CLANG_TIDY_EXE)
    message(STATUS "clang-tidy: ${CLANG_TIDY_EXE}")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
  else()
    message(WARNING "clang-tidy not found — static analysis disabled")
  endif()
endif()

# ── Code coverage (gcov/lcov) ──
if(LOONG_ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    message(STATUS "Coverage: enabled (gcov)")
  elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
    message(STATUS "Coverage: enabled (llvm-cov)")
  else()
    message(WARNING "Coverage: unsupported compiler ${CMAKE_CXX_COMPILER_ID}")
  endif()
endif()

if(LOONG_ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined)
  add_link_options(-fsanitize=address,undefined)
endif()

# ============================================================
# Find Dependencies
# ============================================================

# Required: Threading
find_package(Threads REQUIRED)

# Required: FFmpeg
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)
pkg_check_modules(SWRESAMPLE REQUIRED libswresample)

# Required: OpenCV
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs dnn)

# Required: spdlog
find_package(spdlog REQUIRED)

# Required: nlohmann_json
find_package(nlohmann_json 3.0 REQUIRED)

# Required: SQLite3
find_package(SQLite3 REQUIRED)

# Required: OpenSSL (for HTTP/WebSocket)
find_package(OpenSSL REQUIRED)

# Required: cpp-httplib (header-only HTTP server library)
# Using local copy in third_party/cpp-httplib/
set(HTTPLIB_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/third_party/cpp-httplib)

# Optional: ONNX Runtime (GPU-accelerated inference)
if(LOONG_ENABLE_ONNXRUNTIME)
  find_package(onnxruntime QUIET)
  if(NOT onnxruntime_FOUND)
    # Try pkg-config fallback
    pkg_check_modules(ONNXRUNTIME QUIET libonnxruntime)
    if(NOT ONNXRUNTIME_FOUND)
      # Try manual path (user sets ONNXRUNTIME_ROOT)
      if(DEFINED ONNXRUNTIME_ROOT)
        set(ONNXRUNTIME_INCLUDE_DIRS "${ONNXRUNTIME_ROOT}/include")
        find_library(ONNXRUNTIME_LIB onnxruntime
          PATHS "${ONNXRUNTIME_ROOT}/lib"
          NO_DEFAULT_PATH)
        if(ONNXRUNTIME_LIB)
          set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_LIB})
          set(ONNXRUNTIME_FOUND TRUE)
        endif()
      endif()
    endif()
  endif()

  if(onnxruntime_FOUND OR ONNXRUNTIME_FOUND)
    set(LOONG_HAS_ONNXRUNTIME TRUE)
    message(STATUS "ONNX Runtime: FOUND")
  else()
    message(WARNING "ONNX Runtime: NOT FOUND — ORT backend disabled")
    set(LOONG_HAS_ONNXRUNTIME FALSE)
  endif()
else()
  set(LOONG_HAS_ONNXRUNTIME FALSE)
endif()

# Optional: TensorRT (NVIDIA GPU-optimized inference)
if(LOONG_ENABLE_TENSORRT)
  find_path(TENSORRT_INCLUDE NvInfer.h
    PATHS /usr/include/x86_64-linux-gnu
          /usr/local/tensorrt/include
          $ENV{TENSORRT_ROOT}/include)
  find_library(NVINFER_LIB nvinfer
    PATHS /usr/lib/x86_64-linux-gnu
          /usr/local/tensorrt/lib
          $ENV{TENSORRT_ROOT}/lib)
  find_library(NVONNXPARSER_LIB nvonnxparser
    PATHS /usr/lib/x86_64-linux-gnu
          /usr/local/tensorrt/lib
          $ENV{TENSORRT_ROOT}/lib)

  if(NVINFER_LIB AND NVONNXPARSER_LIB AND TENSORRT_INCLUDE)
    set(LOONG_HAS_TENSORRT TRUE)
    message(STATUS "TensorRT: FOUND (${NVINFER_LIB})")
  else()
    set(LOONG_HAS_TENSORRT FALSE)
    message(WARNING "TensorRT: NOT FOUND — TRT backend disabled")
  endif()
else()
  set(LOONG_HAS_TENSORRT FALSE)
endif()

# Optional: Google Test
if(LOONG_BUILD_TESTS)
  find_package(GTest REQUIRED)
  enable_testing()
endif()

# Optional: Google Benchmark
if(LOONG_BUILD_BENCHMARKS)
  find_package(benchmark REQUIRED)
endif()

# Aggregate FFmpeg libraries
set(FFMPEG_LIBRARIES
  ${AVCODEC_LIBRARIES}
  ${AVFORMAT_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  ${SWSCALE_LIBRARIES}
  ${SWRESAMPLE_LIBRARIES}
)

# ============================================================
# Global Include Directories
# ============================================================
include_directories(
  ${CMAKE_SOURCE_DIR}/src
  ${AVCODEC_INCLUDE_DIRS}
  ${AVFORMAT_INCLUDE_DIRS}
  ${AVUTIL_INCLUDE_DIRS}
  ${SWSCALE_INCLUDE_DIRS}
  ${SWRESAMPLE_INCLUDE_DIRS}
)

# ============================================================
# Sub-directories (Components)
# ============================================================

# COMP-01: Core Framework
add_subdirectory(src/core)

# COMP-02: Video Codec (FFmpeg H.264/H.265)
add_subdirectory(src/codec)

# COMP-05: Video Overlay Library (OSD Renderer + Compositor)
add_subdirectory(src/overlay)

# COMP-03: Pipeline Stages (Input/Decode/AI/Overlay/Output)
add_subdirectory(src/pipeline_stages)

# COMP-04: AI Engine
add_subdirectory(src/ai_engine)

# COMP-06: Storage Management
add_subdirectory(src/storage)

# COMP-07: Network Communication
add_subdirectory(src/network)

# COMP-08: Channel Management
add_subdirectory(src/channel)

# COMP-10: System Management (Auth + User Management)
add_subdirectory(src/system)

# COMP-02 Extension: Video Input (ONVIF)
add_subdirectory(src/video_input)

# FEAT-1.1: Rules Engine (Analysis Rules + Evaluators)
add_subdirectory(src/rules)

# FEAT-4.1: Analytics Aggregation Service
add_subdirectory(src/analytics)

# ============================================================
# Main Executable
# ============================================================
add_executable(loong-ainvr src/main.cc)
target_link_libraries(loong-ainvr
  PRIVATE
    loong_core
    loong_codec
    loong_overlay
    loong_pipeline_stages
    loong_ai_engine
    loong_storage
    loong_network
    loong_channel
    loong_system
    loong_video_input
    loong_rules
    loong_analytics
    Threads::Threads
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${FFMPEG_LIBRARIES}
    ${OpenCV_LIBS}
    SQLite::SQLite3
)

# ============================================================
# Tests
# ============================================================
if(LOONG_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# ============================================================
# Benchmarks (Google Benchmark)
# ============================================================
if(LOONG_BUILD_BENCHMARKS)
  # Pipeline throughput benchmark
  add_executable(pipeline_bench tests/benchmark/pipeline_bench.cc)
  target_link_libraries(pipeline_bench
    PRIVATE
      loong_core
      benchmark::benchmark
      Threads::Threads
  )

  # Memory pool benchmark
  add_executable(memory_bench tests/benchmark/memory_bench.cc)
  target_link_libraries(memory_bench
    PRIVATE
      loong_core
      benchmark::benchmark
      Threads::Threads
  )

  # AI inference benchmark
  add_executable(inference_bench tests/benchmark/inference_bench.cc)
  target_link_libraries(inference_bench
    PRIVATE
      loong_ai_engine
      loong_core
      benchmark::benchmark
      Threads::Threads
  )
endif()

# ============================================================
# Install
# ============================================================
install(TARGETS loong-ainvr DESTINATION bin)
install(FILES config/local.json DESTINATION bin RENAME config.json)
install(DIRECTORY DESTINATION bin/models)
install(DIRECTORY DESTINATION bin/plugins)
install(DIRECTORY DESTINATION bin/recordings)
install(DIRECTORY DESTINATION bin/data)

# Install frontend dist if available (vite outputs to PROJECT_ROOT/dist/web/)
if(EXISTS "${CMAKE_SOURCE_DIR}/dist/web")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/dist/web/" DESTINATION bin/web)
elseif(EXISTS "${CMAKE_SOURCE_DIR}/web/dist")
  install(DIRECTORY "${CMAKE_SOURCE_DIR}/web/dist/" DESTINATION bin/web)
endif()

# ============================================================
# Summary
# ============================================================
message(STATUS "")
message(STATUS "=== Loong AI NVR Build Configuration ===")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler:       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Tests:          ${LOONG_BUILD_TESTS}")
message(STATUS "  Benchmarks:     ${LOONG_BUILD_BENCHMARKS}")
message(STATUS "  ONNX Runtime:   ${LOONG_HAS_ONNXRUNTIME}")
message(STATUS "  ORT CUDA EP:    ${LOONG_ENABLE_ONNXRUNTIME_CUDA}")
message(STATUS "  TensorRT:       ${LOONG_HAS_TENSORRT}")
message(STATUS "  Sanitizers:     ${LOONG_ENABLE_SANITIZERS}")
message(STATUS "  clang-tidy:     ${LOONG_ENABLE_CLANG_TIDY}")
message(STATUS "  Coverage:       ${LOONG_ENABLE_COVERAGE}")
message(STATUS "========================================")
message(STATUS "")
