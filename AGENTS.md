# Working on this project

Read this before changing anything. Most of it is here because it was learned
the expensive way.

## What this is, and what it is not

The audio signal is the subject. The camera is **only a palette**.

The reference is Ryoichi Kurokawa: black field, precise instrument-like
structures, fine granular detail, sharp tearing. Data, not photography.

**It is not a camera feed with effects on it.** An earlier version displaced
camera columns by the waveform, producing a distorted webcam image. That was
wrong and was removed. If a change starts making the camera legible as a
picture, the change is going the wrong way — no matter how good it looks.

The camera's entire contribution is colour variety. It is reduced to a
luminance-sorted ramp of scattered samples (`rebuildPalette`), smoothed frame
to frame.

## Current state

| Element | Function |
|---|---|
| Strata | scrolling spectrogram; frequency vertical, log 30 Hz–16 kHz, low at bottom; brightness = magnitude, hue = palette |
| Waveform | instantaneous trace across the band |
| Profile | "now" spectrum as ticks below the band |
| Tearing | transients displace horizontal slices of history, offsets decay |

Analysis: hand-written radix-2 FFT, 2048-point, Hann-windowed, in `ofApp.cpp`.
No addons — `ofxFft` is not in core and the project is deliberately
dependency-free.

## Traps

Each of these cost a debugging round. They fail silently, which is why they
are listed rather than left to be rediscovered.

1. **`cam.setup(w, h, false)`** — the third argument is `bTexture`, not a
   "use defaults" flag. Passing `false` skips texture creation, so
   `getTexture()` never allocates. Every frame check then reads as a dead
   camera and the app silently falls back. This produced several rounds of
   review of a fallback image that was mistaken for the real output.

2. **FFT normalisation.** An N-point FFT of a full-scale windowed sine peaks
   near `A·N/4` (Hann coherent gain 0.5). Unnormalised, every bin clamps to 1
   and the spectrogram is a featureless bright wall. Divide by `kFftSize/4`.

3. **Log frequency mapping.** Linear FFT bins crowd everything musical into
   the leftmost sliver. Bins are log-mapped and each display bin averages the
   FFT bins inside it; without the averaging the top end aliases into sparkle.

4. **Palette must not carry geometry.** Mapping bin → camera x with a drifting
   sample row makes consecutive spectrogram columns adjacent camera rows, and
   scrolling reassembles the actual photograph. Sample scattered positions and
   sort by luminance instead.

5. **`NSHighResolutionCapable`** was an unsubstituted `${...}` string in the
   Makefile build, so the app rendered non-Retina and was upscaled. It is now
   literal `<true/>` in `openFrameworks-Info.plist`.

6. **Camera permission blocks.** `ofVideoGrabber::setup()` does not return
   until the system prompt is answered. It is requested asynchronously
   (`requestCameraAccess`) so the window still appears, and a camera that opens
   but never delivers is detected after 10s and reported.

## Verifying a change

You cannot judge this from source. Render and look at the image.

```sh
cd bin/audioVisualizer.app/Contents/MacOS
./audioVisualizer --selftest renders/try.png --source camera
./audioVisualizer --selftest renders/try.png --source some.jpg   # palette from a photo
./audioVisualizer --selftest renders/try.png                     # built-in ramp
```

Writes under `bin/data/` and exits. Opens no audio device: the analysis is fed
a synthetic harmonic sweep through the same ring buffer the live path fills, so
every code path under test is the real one.

Override any tunable without rebuilding:

```sh
./audioVisualizer --selftest renders/hot.png --glitch 2.0 --flux 0.08 --scroll 5
```

`scroll band glitch flux gain smoothing saturation wavealpha floordb`

**Do not delete renders.** They are the shared record of what a change did, and
the person reviewing needs to see them. `bin/data/renders/` is committed.

Two limits of the self-test, both of which have caused wrong conclusions:

- The signal is **synthetic**, so the shapes are fixed between runs. It shows
  layout and palette. It says nothing about response to music — that needs a
  live run.
- A camera on a blank wall yields a flat grey palette at every setting. If
  renders look washed, check what the camera is pointed at before tuning.

## Running live

```sh
make -j8 Release && make RunRelease
```

Microphone by default, so it hears the speakers. Clean playback capture needs
BlackHole plus a Multi-Output Device — see README. `h` toggles the HUD, which
carries the device, the parameters and a level meter; a red meter reading
silent means the input, not the renderer, is the problem.

## Constraints

- Must sit at `openFrameworks/apps/myApps/audioVisualizer`; the build resolves
  the core through `../../..`.
- openFrameworks 0.12.1, macOS, arm64. Default GL2 renderer.
- No addons.
- Drawing is plain openFrameworks — there is no shader any more. The previous
  GLSL displacement shader was deleted with the camera-distortion renderer.

## Open

- **A red annotation sits on `bin/data/renders/K1_default.png`**, drawn around
  the spectrum profile ticks, and its meaning was never settled. It reads
  either as "this tick bar is too dense and slabs out" or as "more of this fine
  texture". Ask before acting on it. Note that render also predates the palette
  fix and still shows the camera-reconstruction artifact described in trap 4.
- The piece has never been tuned against sustained real music, only short live
  runs and synthetic sweeps. Glitch defaults are conservative.
- Nothing has been profiled. `rebuildPalette` samples 96 pixels per frame and
  `pushColumn` does one FBO blit; neither has been measured.
