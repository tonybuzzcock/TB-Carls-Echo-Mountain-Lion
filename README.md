# TB Carls Echo (Mountain Lion VST)

A model of the **Carlsbro Echo** (the big mains-powered analogue delay pedal) as a
**VST 2.4, 64-bit Intel plugin for Mac OS X 10.8.5** (Pro Tools 11 through Blue Cat
PatchWork). Non-commercial, for personal use. Built and tested on GitHub Actions.

![panel](https://raw.githubusercontent.com/tonybuzzcock/TB-Carls-Echo/gui-preview/panel_showcase.png)

## The circuit

Modelled from Dirk Hendrik's drawing (rev P, 23 Feb 2012, traced from a real unit;
`Docs/Carlsbro_Echo_schematic.pdf`). A Reticon R5101 CCD between an NE571 compander,
with Sallen-Key filters either side:

- **Input**: C1 470n, IC1.C follower (RC4136). Its output is the DRY signal: the DRY
  OUTPUT jack, the bottom of MIX and the bypass.
- **Compressor** IC2.B (NE571): input through R5 10K, the repeats through R6 6K8.
- **IC1.D**: a Sallen-Key low-pass (R9 12K, R10 15K, C45 / C11 4n7) with a gain network
  (R13 22K over R12 27K and R11 18K + C10 10n) that gives x1.8 in the bass and x3 in the
  treble, so it peaks at about 2-3 kHz. Then two 3rd-order low-pass sections (IC1.A, IC1.B).
- **VR1.B** (the DELAY pot's second gang) with R20 2K2 + C19 4n7: a tone filter that darkens
  as the delay gets longer.
- **IC3 R5101** CCD, clocked by IC4 (CD4007) through R33 3K9 + VR1.A; the drawing gives the
  measured clock range, 10.9 kHz to 125 kHz.
- Outputs through R29 / R30 and the OUTPUT TRIM into IC5.D (x1.47, inverting), then two more
  3rd-order low-pass sections (IC5.A, IC5.B).
- **Expander** IC2.A (NE571), C35 to TP24: the echo.
- **SWELL**: TP24 -> R41 2K2, C38 47n, VR2 22K lin -> C39 -> R6 back into the compressor.
- **MIX**: VR3 22K lin between DRY and the echo; S1 picks the MIX wiper or DRY; IC5.C out.

Each stage is modelled from its components (op-amp stages as their transfer functions,
rail clipping on the RC4136s and NE571s). The NE571s are gain cells driven by their own
rectifiers (C6 / C37 680n with the internal 10K), so the compander breathes like the real
one. The CCD is a clocked sample-and-hold chain, so the clock aliasing and the staircase
are there. 4x oversampled.

### Readings to check against your pedal

- **VR1.B direction**: the drawing's rotation arrows would make the tone filter darkest at
  the *shortest* delay. The model has it the other way round (darkest at the longest delay,
  where the CCD's bandwidth is lowest), which is what the filter is for.
- **Treble peak**: as drawn, IC1.D peaks at about 2-3 kHz and nothing after the expander takes
  it out again. The compander doubles it, so the repeats come back strongly bright around
  2 kHz (about +20 dB on a single tone at short delays, +7 dB at the longest). That also sets
  where SWELL runs away: from about 5 at short delays, 7 at mid delays and 9-10 at the longest.

### Estimates (not fixed by the drawing)

Named constants in `Source/DSP/Echo.cpp`:

| What | Value |
|---|---|
| R5101 delay | 2000 clock periods: 16 ms (DELAY min) to 183 ms (DELAY max), 33 ms at noon |
| Clock law | f = A / (VR1.A + R0), fitted to the measured 10.9 / 125 kHz ends (R0 = 4K5) |
| CCD gain and clipping | x0.40, soft clip at 2 V peak (BBD BIAS centred) |
| NE571 unity-gain level | 0.70 V average (0 dBu); compressor gain limited to 40 dB |
| Supply | VCC 11.2 V (Z1 11V8), op-amp swing +/- 4.1 V about VBIAS |
| Delay pot taper | audio (log), 10% at mid-rotation |

## Controls

| Panel | Parameter | What it is |
|---|---|---|
| DELAY | `Delay` | VR1, 47K log dual: the clock and the tone filter. Shows the delay in ms |
| SWELL | `Swell` | VR2, the repeats (0-10, marked min/max) |
| MIX | `Mix` | VR3, direct .. echo (0-10, marked direct/echo) |
| Stomp switch | `Effect` | S1: In / Out (Out gives the buffered dry signal, as on the pedal). Glows red when in |
| INPUT (strip) | `Input` | -12..+12 dB into the pedal (1.0 in the DAW = 1 V) |
| OUTPUT (strip) | `Output` | -24..+24 dB after it |

Defaults follow the photo of the pedal: DELAY 7, SWELL 3, MIX 5. Eight preset slots (SAVE,
then a slot) live in `~/Library/Application Support/TB/TB Carls Echo/Presets.txt`, shared by
every instance.

## Install

1. GitHub -> **Actions** -> the latest green run -> **Artifacts** -> `TB-Carls-Echo-mac-vst`.
2. Unzip it (twice: GitHub zips the zip) to get `TB Carls Echo.vst`.
3. Copy `TB Carls Echo.vst` to `/Library/Audio/Plug-Ins/VST/`.
4. Rescan in Blue Cat PatchWork and load **TB Carls Echo**.

## Building and testing

- `make -C Tests run vst-run` - DSP tests (delay times, echo response, MIX, compander, SWELL,
  bypass, limits and stability, automation, CPU, demo WAVs in `Tests/renders`), presets, and the
  plugin in a mini VST host. `make -C Tests asan` runs them under the sanitizers.
- `scripts/build_vst_mac.sh` (on a Mac) builds the bundle for 10.8 and fails unless the
  binary declares 10.8 as its minimum and links only libraries that exist on 10.8.
- The workflow does both, opens the panel offscreen and clicks every control, and
  publishes the screenshots to the `gui-preview` branch.

VST ID `TBce`. Plain C++98 DSP, no Steinberg SDK (own ABI declarations in `vst2_abi.h`).
