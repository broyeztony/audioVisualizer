# audioVisualizer

Audio is the subject; the camera is only a palette.

The image is built from the signal itself — a scrolling spectrogram, an
instantaneous waveform trace, a spectrum profile — drawn on black. The camera
never appears as a picture. It is reduced to a luminance-sorted ramp of
scattered samples, so the palette tracks whatever is in front of the lens while
carrying none of its geometry.

Built against openFrameworks 0.12.1.

Continuing this work: see [AGENTS.md](AGENTS.md).

## Where this has to live

The build resolves openFrameworks through a relative path (`../../..`), so the
project only compiles from inside an openFrameworks tree:

```
openFrameworks/
  apps/
    myApps/
      audioVisualizer/   <- this repository
```

Download the 0.12.1 macOS release from <https://openframeworks.cc/download/>,
unpack it, and clone into `apps/myApps/`. Cloning anywhere else will fail to
find the core headers.

## Build and run

```sh
cd openFrameworks/apps/myApps/audioVisualizer
make -j8 Release
make RunRelease
```

Accept the camera and microphone prompts on first launch. Denying the camera
costs only the palette — it falls back to a built-in cool-to-warm ramp and
everything else is unchanged.

## What is on screen

| | |
|---|---|
| **Strata** | scrolling spectrogram, newest at the right. Frequency is vertical, log-mapped 30 Hz – 16 kHz, low at the bottom. Brightness is magnitude; hue comes from the palette. |
| **Waveform** | the instantaneous trace, thin and white, across the band. |
| **Profile** | the "now" spectrum as fine ticks under the band — the slice the strata above is a history of. |
| **Tearing** | transients displace horizontal slices of the history, then decay. |

A sustained tone draws a horizontal line; a sweep draws a curve; a percussive
hit tears the strata. Everything you see is the signal.

## Capturing playback audio

By default it listens to the **microphone**, so it reacts to whatever the
speakers are playing. That works with no setup.

For a clean signal off the sound card:

```sh
brew install --cask blackhole-2ch      # asks for your password: installs a driver
```

Then open **Audio MIDI Setup**, `+` → **Create Multi-Output Device**, tick both
**MacBook Pro Speakers** and **BlackHole 2ch**, set it as the system output, and
press `d` in the app until the HUD shows `BlackHole 2ch`. Without the
Multi-Output step, selecting BlackHole alone silences the speakers.

## Controls

| Key | |
|---|---|
| `d` / `D` | next / previous audio input device |
| `c` | next camera |
| drag a file onto the window | use a still image as the palette source |
| `↑` / `↓` | glitch amount |
| `←` / `→` | transient sensitivity (flux threshold) |
| `e` / `Q` | scroll speed |
| `w` / `q` | band height |
| `[` / `]` | magnitude smoothing |
| `-` / `=` | manual gain |
| `a` | auto-gain on/off |
| `,` / `.` | palette saturation |
| `t` | timestamp on/off |
| `h` | HUD on/off |
| `f` | fullscreen |
| `s` | save a frame to `bin/data/renders/` |

If the level meter sits red and reads silent, the selected input is receiving
nothing — press `d`.

## Rendering stills

```sh
cd bin/audioVisualizer.app/Contents/MacOS
./audioVisualizer --selftest renders/a.png                    # built-in ramp
./audioVisualizer --selftest renders/b.png --source photo.jpg # photo as palette
./audioVisualizer --selftest renders/c.png --source camera
```

Output lands under `bin/data/`. Nothing clears that directory — renders
accumulate so they can be compared.

Any tunable can be overridden without rebuilding:

```sh
./audioVisualizer --selftest renders/hot.png --source camera \
    --glitch 2.0 --flux 0.08 --scroll 5 --saturation 1.4
```

`scroll`, `band`, `glitch`, `flux`, `gain`, `smoothing`, `saturation`,
`wavealpha`, `floordb`.

A self-test drives the analysis from a **synthetic** harmonic sweep, not live
audio, so the shapes are fixed between runs — useful for judging palette and
layout, useless for judging how it responds to music.

## Notes on the analysis

FFT is a hand-written radix-2 Cooley-Tukey (2048-point, Hann-windowed) rather
than an addon, so the project stays dependency-free — `ofxFft` is not in core.

Two details matter more than they look:

- **Normalisation.** An N-point FFT of a full-scale windowed sine peaks near
  `A·N/4`. Unnormalised, every bin clamps to full brightness and the
  spectrogram is a solid wall rather than structure.
- **Log frequency mapping.** Linear FFT bins put everything musical in the
  leftmost sliver. Bins are mapped logarithmically across 30 Hz – 16 kHz, and
  each display bin averages the FFT bins inside it so the top end does not
  alias into sparkle.
