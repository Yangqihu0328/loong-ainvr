# =============================================================================
# Loong AI NVR — Multi-Stage Dockerfile
# =============================================================================
# Stage 1 (frontend):  Node.js → Build Vue.js frontend
# Stage 2 (builder):   Ubuntu 22.04 → Build C++ backend
# Stage 3 (runtime):   Ubuntu 22.04 minimal → Production image
# =============================================================================

# ---------------------------------------------------------------------------
# Stage 1: Frontend Build
# ---------------------------------------------------------------------------
FROM node:20-slim AS frontend

WORKDIR /app/web
COPY web/package.json web/package-lock.json ./
RUN npm ci --no-audit --no-fund
COPY web/ ./
RUN npm run build

# ---------------------------------------------------------------------------
# Stage 2: C++ Backend Build
# ---------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    gcc-11 g++-11 \
    libavcodec-dev libavformat-dev libavutil-dev \
    libswscale-dev libswresample-dev \
    libopencv-dev \
    libspdlog-dev nlohmann-json3-dev libsqlite3-dev \
    libssl-dev \
    libgtest-dev libgmock-dev \
    libsrtp2-dev \
    libwebsockets-dev \
    libpaho-mqtt-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt ./
COPY src/ ./src/
COPY third_party/ ./third_party/
COPY tests/ ./tests/
COPY config/ ./config/

RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=gcc-11 \
    -DCMAKE_CXX_COMPILER=g++-11 \
    -DLOONG_BUILD_TESTS=ON \
    -DLOONG_BUILD_BENCHMARKS=OFF \
    && cmake --build build --parallel "$(nproc)"

# Run tests during build to catch issues early
RUN cd build && ctest --output-on-failure --timeout 120

# ---------------------------------------------------------------------------
# Stage 3: Production Runtime
# ---------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

LABEL maintainer="Loong AI NVR Project"
LABEL description="Loong AI NVR - Embedded Intelligent Video Analysis System"
LABEL version="0.3.0"

RUN apt-get update && apt-get install -y --no-install-recommends \
    libavcodec58 libavformat58 libavutil56 \
    libswscale5 libswresample3 \
    libopencv-core4.5d libopencv-imgproc4.5d \
    libopencv-imgcodecs4.5d libopencv-dnn4.5d \
    libspdlog1 libsqlite3-0 \
    libssl3 \
    libsrtp2-1 \
    libwebsockets16 \
    libpaho-mqtt3as \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -r -s /bin/false -m -d /opt/loong-ainvr loong

WORKDIR /opt/loong-ainvr

COPY --from=builder /build/build/loong-ainvr ./loong-ainvr
COPY --from=builder /build/config/default.json ./config/default.json
COPY --from=frontend /app/web/dist ./web/

RUN mkdir -p /recordings /models /plugins \
    && chown -R loong:loong /opt/loong-ainvr /recordings /models /plugins

VOLUME ["/recordings", "/models", "/plugins"]

# HTTP API + Web UI
EXPOSE 8080
# HTTP-FLV / WebSocket Live Stream
EXPOSE 8081
# RTSP Server
EXPOSE 554

USER loong

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -sf http://localhost:8080/api/health || exit 1

ENTRYPOINT ["./loong-ainvr"]
CMD ["config/default.json"]
