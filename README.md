# vinna

**vinna** is a small command line tool that keeps movie audio at a comfortable level.

Films have a huge dynamic range: dialogue is mixed quietly, while explosions, gunshots and
music are much louder. You turn the volume up to hear people talk, and a few minutes later
you are rushing for the volume key so the neighbours don't complain. vinna does that for
you: it listens to what your computer is playing and, when it gets too loud, turns the
player's volume down quickly. When the scene calms down it brings the volume back slowly
to the level *you* chose.

```
 ┌──────────────┐  samples  ┌─────────────┐  loudness  ┌────────────┐  volume  ┌────────────────┐
 │ AudioCapture │ ────────► │ LevelMeter  │ ─────────► │ Controller │ ───────► │ VolumeActuator │
 │ (what is     │           │ K-weighted  │            │ attack /   │          │ VLC, Pulse or  │
 │  playing)    │           │ LUFS window │            │ hold /     │          │ Windows mixer  │
 └──────────────┘           └─────────────┘            │ release    │          └────────────────┘
        ▲                                              └────────────┘                  │
        └────────────────────────── you hear the adjusted sound ◄──────────────────────┘
```

- Works on **Linux** (PulseAudio or PipeWire) and **Windows** (WASAPI).
- Controls only the player (VLC by default), not your whole system volume.
- Measures loudness like broadcasters do (ITU-R BS.1770 / EBU R128 K-weighting), so bass
  rumble does not trigger it as much as a scream does.
- Restores your volume when you quit (Ctrl+C or closing the window). While the
  volume is lowered, your original volume is also saved to `restore.state` next
  to the config file, so if vinna is killed or crashes it is put back the next
  time vinna runs, or right away with `vinna restore`.
- If you move the volume yourself while it runs, that becomes the new reference.
- No dependencies to install: [miniaudio](https://miniaud.io) is bundled.

## Quick start

```bash
# 1. See what vinna hears while a film is playing (changes nothing).
vinna monitor

# 2. Let it control VLC.
vinna run
```

`vinna monitor` shows a live meter. The `|` mark is the threshold. Watch a loud scene and a
dialogue scene, then pick a threshold between them, e.g. `vinna run --threshold -24`.

While `run` is active you see one status line:

```
 -14.2 LUFS [=========================|####          ]  gain  -4.6 dB  volume  59%
```

## How the volume is controlled

| Backend    | Platform | What it changes                         | Setup |
|------------|----------|-----------------------------------------|-------|
| `pulse`    | Linux    | VLC's stream volume in the system mixer | needs `pactl` (package `pulseaudio-utils`, also works with PipeWire) |
| `wasapi`   | Windows  | VLC's slider in the Windows Volume Mixer | nothing |
| `vlc-http` | both     | VLC's own volume, through its web interface | enable the web interface (below) |
| `none`     | both     | nothing (testing)                       | |

`--backend auto` (the default) uses `vlc-http` if you gave a VLC password, otherwise `wasapi`
on Windows and `pulse` on Linux. Use `--app <name>` to control another player
(e.g. `--app mpv`).

### Enabling VLC's web interface (only for `vlc-http`)

1. VLC → *Tools → Preferences*, select **All** at the bottom left.
2. *Interface → Main interfaces*: tick **Web**.
3. *Interface → Main interfaces → Lua*: set a **Password**.
4. Restart VLC, then: `vinna run --vlc-password <your password>`.

## Options

```
vinna run      [options]   Listen and turn the volume down when it gets loud
vinna monitor  [options]   Show the loudness vinna hears (changes nothing)
vinna devices              List audio devices for --device
```

| Option | Default | Meaning |
|--------|---------|---------|
| `--threshold <LUFS>` | `-20` | Start reducing above this loudness. Lower reacts earlier. |
| `--ratio <n>` | `4` | Strength of the reduction (1 = none, 10+ ≈ limiter). |
| `--max-cut <dB>` | `20` | Never go further than this below your volume. |
| `--attack <time>` | `150ms` | How fast the volume goes down. |
| `--hold <time>` | `1s` | How long to wait after a loud moment before going back up. |
| `--release <time>` | `3s` | How slowly the volume goes back up. |
| `--gate <LUFS>` | `-55` | Below this it is silence: the volume is left untouched. |
| `--backend <name>` | `auto` | `auto`, `pulse`, `wasapi`, `vlc-http`, `none`. |
| `--app <name>` | `vlc` | Application to control (part of the process name). |
| `--vlc-host/-port/-password` | `127.0.0.1`, `8080` | VLC web interface. |
| `--device <name>` | system output | Capture device, see `vinna devices`. |
| `--window <time>` | `400ms` | Loudness measurement window. |
| `--tick <time>` | `50ms` | Control loop interval. |
| `--latency <time>` | `120ms` | Delay between a volume change and hearing it in the capture. |
| `--min-step <dB>` | `0.5` | Smallest volume change worth sending. |
| `--config <file>` | | Read options from a file. |
| `--dry-run` | | Compute everything, never touch the volume. |
| `--verbose` | | Print each volume change with a timestamp. |

Times accept `ms`, `s` or `m`.

### Config file

Options can be stored in a file using the same names without dashes
(see [`config/vinna.example.toml`](config/vinna.example.toml)):

```toml
threshold = -22
release = "4s"
app = "vlc"
```

vinna loads it automatically from `~/.config/vinna/vinna.toml` (Linux) or
`%APPDATA%\vinna\vinna.toml` (Windows). Command line options override the file.

## Tuning tips

- **It reacts too late / too little**: lower `--threshold` or raise `--ratio`.
- **The volume "pumps" up and down**: raise `--release` (e.g. `5s`) or `--hold`.
- **Dialogue stays too quiet after an explosion**: lower `--release` (e.g. `2s`).
- **The volume keeps sinking to the maximum cut**: vinna is not hearing the effect of its
  own changes. Make sure the captured device (`vinna devices`) is the one VLC plays to.

## Building

Requires a C++17 compiler. Build with either **xmake** or **CMake**.

### xmake

```bash
xmake
xmake run vinna monitor

# tests
xmake build vinna_tests && xmake run vinna_tests
```

### CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/vinna monitor

# tests
ctest --test-dir build --output-on-failure
```

On Windows, both work with Visual Studio (MSVC) or MinGW. MinGW builds are linked
statically, so `vinna.exe` runs without extra DLLs.

### Docker (Linux)

The image builds vinna and runs the unit tests, so `docker build .` also works as
a quick check on any machine:

```bash
docker build -t vinna .
docker run --rm vinna help
```

To run vinna from a container on a Linux desktop (PulseAudio or PipeWire), use
Compose. It shares the host's audio socket with the container and uses the host
network so the `vlc-http` backend can reach VLC on `127.0.0.1`:

```bash
cp config/vinna.example.toml config/vinna.toml   # then adjust it
docker compose up --build
```

Stopping the container (Ctrl+C or `docker compose down`) restores the volume.
The container runs as UID 1000 by default; if your user has another UID, run
`UID=$(id -u) docker compose up --build`. A container cannot reach Windows or
macOS audio, so on those systems run vinna directly.

## Project layout

```
include/vinna/          public headers (meter, controller, config, interfaces)
src/core/               platform independent logic
src/capture/            audio capture (miniaudio: WASAPI loopback, Pulse monitor)
src/actuators/          volume backends (pulse, wasapi, vlc-http)
src/net/                tiny HTTP client for the VLC web interface
src/app/                command line interface and control loop
tests/                  unit tests (no framework needed)
third_party/            miniaudio (single header)
```

## How it works

1. **Capture** – vinna records what is playing: the *monitor* of the default output on
   Linux, a WASAPI *loopback* stream on Windows.
2. **Measure** – each block is K-weighted and averaged over a 400 ms window, giving
   loudness in LUFS (the same scale TV and streaming services use).
3. **Compensate** – the capture already contains vinna's own volume change, so each block
   is corrected by the gain that was active `--latency` ago. The controller therefore
   sees the loudness of the film itself, not of the adjusted output.
4. **Decide** – above the threshold, the gain follows `-(level - threshold) × (1 - 1/ratio)`
   with a fast attack; once the scene is calm for `--hold`, it returns towards 0 dB with a
   slow release. During silence the gain is frozen.
5. **Apply** – the new volume is `your volume × gain`, sent to the player only when it
   changed by at least `--min-step`.

### Limitation

vinna reacts to sound that is already playing, so the first ~0.2–0.4 s of a sudden
explosion still gets through before the volume drops. Removing that would require
processing the audio *inside* the playback path with look-ahead (a real compressor).
