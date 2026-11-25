# syntax=docker/dockerfile:1

# vinna in a Linux container.
#
# The image builds vinna, runs the unit tests (the build fails if they fail)
# and ships only the binary plus the PulseAudio client libraries.
# At run time it needs the host's PulseAudio / PipeWire socket; see
# compose.yaml. Windows audio (WASAPI) cannot be reached from a container.

# ---- build ------------------------------------------------------------------
FROM debian:bookworm-slim AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY third_party third_party
COPY include include
COPY src src
COPY tests tests

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)" \
 && ctest --test-dir build --output-on-failure

# ---- runtime ----------------------------------------------------------------
FROM debian:bookworm-slim

# libpulse0: loaded by miniaudio to capture what is playing.
# pulseaudio-utils: `pactl`, used by the pulse backend to change VLC's volume.
RUN apt-get update \
 && apt-get install -y --no-install-recommends libpulse0 pulseaudio-utils \
 && rm -rf /var/lib/apt/lists/*

# Run as a regular user whose UID matches the desktop user (usually 1000),
# so the host's audio socket accepts the connection.
ARG UID=1000
RUN useradd --create-home --uid "${UID}" vinna \
 && mkdir -p /home/vinna/.config/vinna \
 && chown -R vinna:vinna /home/vinna/.config

COPY --from=build /src/build/vinna /usr/local/bin/vinna

USER vinna
ENV PULSE_SERVER=unix:/run/pulse/native

ENTRYPOINT ["vinna"]
CMD ["run"]
