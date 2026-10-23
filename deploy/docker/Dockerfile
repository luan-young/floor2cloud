# syntax=docker/dockerfile:1
#
# One multi-stage image definition for every C++ service (spec § 8.1): the builder stage builds
# all targets once; each runtime stage copies one binary. Build from the repository root:
#
#   docker build -f deploy/docker/Dockerfile --target modbus-machine -t f2c/modbus-machine .

# --- builder ----------------------------------------------------------------------------------
FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf autoconf-archive automake build-essential ca-certificates cmake curl g++-13 git \
        jq libtool ninja-build pkg-config tar unzip zip \
    && rm -rf /var/lib/apt/lists/*

# vcpkg at the manifest's builtin-baseline, so the image resolves the same port versions as
# developers and CI. Only vcpkg.json is copied first, so this layer survives source changes.
ENV VCPKG_ROOT=/opt/vcpkg \
    VCPKG_DISABLE_METRICS=1 \
    VCPKG_DEFAULT_BINARY_CACHE=/root/.cache/vcpkg/archives
COPY vcpkg.json /src/vcpkg.json
RUN git init "$VCPKG_ROOT" \
    && git -C "$VCPKG_ROOT" fetch --depth 1 https://github.com/microsoft/vcpkg.git \
        "$(jq -r '.["builtin-baseline"]' /src/vcpkg.json)" \
    && git -C "$VCPKG_ROOT" checkout FETCH_HEAD \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
COPY . .
# The cache mount keeps vcpkg's binary packages across builds; the build tree stays in the layer.
RUN --mount=type=cache,target=/root/.cache/vcpkg/archives \
    mkdir -p "$VCPKG_DEFAULT_BINARY_CACHE" \
    && cmake --preset release -DCMAKE_CXX_COMPILER=g++-13 -DF2C_BUILD_TESTS=OFF \
    && cmake --build --preset release \
    && cmake --install build/release --prefix /opt/f2c

# --- runtime base -----------------------------------------------------------------------------
# Dependencies are linked statically (ADR-0005), so the base image's libstdc++ is all we need.
FROM ubuntu:24.04 AS runtime
USER ubuntu
WORKDIR /home/ubuntu

# --- modbus-machine ---------------------------------------------------------------------------
FROM runtime AS modbus-machine
COPY --from=builder /opt/f2c/bin/f2c-modbus-machine /usr/local/bin/
EXPOSE 502
HEALTHCHECK --interval=5s --timeout=3s --start-period=5s --retries=3 \
    CMD test $(( $(date +%s) - $(stat -c %Y /tmp/f2c-healthy) )) -lt 15
ENTRYPOINT ["f2c-modbus-machine", "--health-file", "/tmp/f2c-healthy"]
