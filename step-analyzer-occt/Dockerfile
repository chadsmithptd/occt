# step-analyzer-occt: C++ REST API for STEP analysis using OpenCascade (OCCT).
# Compatible with Render.com Docker deployment.
# System dependencies: OCCT (libocct-*), CMake, GCC, Git (for FetchContent).

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# System dependencies: OCCT (OpenCascade) and build tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    cmake \
    g++ \
    git \
    libocct-data-exchange-dev \
    libocct-foundation-dev \
    libocct-modeling-algorithms-dev \
    libocct-modeling-data-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY CMakeLists.txt ./
COPY cmake ./cmake/
COPY src ./src/

# Build (FetchContent will download nlohmann/json and cpp-httplib)
RUN mkdir build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=Release \
    && cmake --build . -j$(nproc) \
    && cp step-analyzer-occt /usr/local/bin/

# Runtime: only the binary is needed (OCCT libs are linked statically or from system)
WORKDIR /tmp

# Render.com sets PORT; default 10000
ENV PORT=10000
EXPOSE 10000

CMD ["step-analyzer-occt"]
