# Grus

**The manual** is in `docs/` and is published with GitHub Pages: the panel, every page, the engines and the effects, with screenshots.

This is the sound and sequencer core of **Grus**, a groovebox firmware for the M-VAVE FM-1. It is plain C99 with no hardware dependencies, ported from the browser prototype.

The core covers:
- **Engines:** the seven engines, Gnista, Klang, Puls, Moln, Rost, Stöt and Skärva.
- **Voice pool:** shared by all eight tracks.
- **Sequencer:** the lock sequencer with chance, ratchets, conditions, speeds and pendulum.
- **Motion page:** DJ filter, LFO and pump.
- **Master:** reverb, three delay types, speaker EQ and the limiter.

It also holds the live FX layer (repeat, reverse, tape stop, filters, freeze, dub) and the requests the panel sends it: play, pattern loads at the pattern's end, notes from the keys.

## The firmware: `app/`

Grus runs on [fm1-base](https://github.com/charlesvestal/fm1-base), the platform layer that handles the FM-1's screen, keys, LEDs, audio, MIDI, storage and updates. `app/` is the app on top of it.

| File | What |
|---|---|
| `app.h`, `fm1.toml` | The app's contract and build settings (o2 units, `ram_layout = "packed"`, no Bluetooth yet) |
| `app.c` | Panel, pages, lights, the audio callback (`app_render` calls `grus_render`), MIDI clock, chains and the song |
| `store.c` | Patterns packed for the flash (8 slots in a 96 KB pool) and the undo history in RAM |
| `params.c` | Every page's knobs: names, steps, where the value lives, how it is shown |
| `gfx_grus.c` | Anti-aliased discs, rings, arcs, pills and lines on fm1-base's canvas; text with å ä ö |
| `tests/tour.txt` | A scripted walk through every page for the host simulator (screenshots and a WAV) |
| `tests/arp.txt` | The arpeggiator: held keys, latch, recording into the steps |
| `tests/features.txt` | Saving and a reboot, chains, the song, live FX, REC, RANDOM, undo, copy and paste, setup, MIDI clock in |

**Done:**
- Start-up animation, STEPS (16 circles, pages, playhead), the step page (hold a step), locks (hold steps on SOUND, MOTION or TRACK and turn a knob).
- SOUND, MOTION, TRACK, EFFECTS, MIXER (meters, four rows), VISUAL (pills, grains, step clock), PATTERNS (factory patterns, switched at the pattern's end).
- Sound presets on PRESET, tempo on ALGORITHM, tracks on SELECT and the black keys, M (mute) and R (clear step, clear track with a question).

- Saving: keys 9-16 on PATTERNS save (with a question), the slot then loads the saved pattern, also after a restart. R and a key 1-8 gives the slot back to the factory.
- Chains: hold a slot key on PATTERNS and press more. SONG (SAVE twice): rows with repeats, add, delete, chain to song, play from a row, loop or stop at the end.
- REC: tap REC and the keys play the track and write into the step playing. Hold REC: scale, key, chord, octave.
- LIVE-FX: hold FX, then hold white keys 1-9 for effects and 16 for fill.
- RANDOM (ENV): seven step actions, six sound actions, ENV again switches.
- K copies a step or a track, the next one pastes. Hold SAVE: undo (24 steps back).
- SETUP (hold GLO): limiter, ceiling, speaker EQ, MIDI clock in or out, night mode, brightness. Kept in the flash.

- ARP: tap it for VISUAL, hold it for the arpeggiator: up, down, up-down, as played, random, chord; 1/32 to 1/4 and 1/16 triplets; 1-4 octaves, gate, latch, and recording into the steps. It plays the keys while REC is on, on the selected track, on the step clock (swing too) while the sequencer runs.

**Not yet:** loading your own samples, Bluetooth MIDI.

### Build (Linux, or WSL on Windows)

```
git clone --recursive https://github.com/charlesvestal/fm1-base ~/fm1-base
export JIELI_TOOLCHAIN=~/.jieli/toolchain      # the toolchain Felucca's get_toolchain.sh installed
cd ~/fm1-base
python3 tools/build.py ~/grus/app              # the firmware package: ~/grus/app/build/
python3 tools/build.py ~/grus/app --host       # the host simulator (no toolchain needed)
~/grus/app/build/host/grus_host ~/grus/app/tests/tour.txt ~/grus/app/build/host
```

The JieLi AC79 SDK is looked for in `~/fw-AC79_AIoT_SDK`, the same place as for Felucca.

## Layout

| Path | What |
|---|---|
| `src/grus.h` | The core's API and data types |
| `src/grus.c` | Engines, sampler, voice pool, sequencer, effects |
| `src/grus_data.c` | Generated parameter tables, the 8 factory patterns, the 4 factory samples as 4-bit ADPCM flash bytes |
| `docs/` | The manual: `index.html`, screenshots and a demo (GitHub Pages serves it) |
| `tools/export.js` | Pulls the tables, patterns and samples out of the prototype (needs Playwright) |
| `tools/gen_data.py` | Writes `grus_data.c` from the export |
| `tools/reference.js` | Renders the factory patterns in the prototype with a fixed random source |
| `test/compare.c` | Compares the C output with the prototype's, sample by sample |
| `test/bench.c` | CPU cost per engine and per pattern |
| `test/render.c` | Renders patterns to a WAV file |
| `test/solo.c` | Renders one track alone, for comparing builds track by track |

## Build and check

```
make                                   # builds compare_d, compare_f, bench and render
node tools/reference.js tools/grus-prototype.html 3  # reference renders from the prototype
./compare_d                            # double build against the prototype
./bench
./render out.wav 8                     # all 8 factory patterns, 8 s each
```

There are two builds:
- **Double (`-DGRUS_DOUBLE`):** computes in double precision, like JavaScript. It is the porting check.
- **Float (the default):** what runs on the FM-1. It uses a sine table and polynomial approximations instead of library calls in the per-sample code.

## Status, 8 November 2026

**Built for the FM-1's limits** (from fm1-base and felucca-bsp):
- **CPU:** a single-precision FPU and no double, so the device build uses only `float`.
- **Maths:** no libm. All maths is fm1-base's `fastmath.h` (`third_party/`, MPL-2.0).
- **RAM:** about 430 KB in all, and the core needs 237 KiB of it:

  | Part | Size |
  |---|---|
  | Delay | 128 KiB |
  | Reverb | 48 KiB |
  | Working pattern | 17 KiB |
  | Track state with the sampler's caches | about 37 KiB |
  | Sample seek tables | 8 KiB |

- **Samples:** they stay in flash as 4-bit ADPCM, decoded 256 samples at a time from a seek table.
- **Patterns:** steps are packed to 34 bytes. The factory patterns are sparse, holding only the steps that are used.
- **64-bit multiplies** sit in `noinline` helpers, because of the CPU erratum in felucca-bsp's docs/CPU.md.

**Accuracy**
- The double build matches the prototype in all 8 factory patterns, 148 to 153 dB below the signal. That is just float32 rounding of the output.
- The prototype renders with the same 16-bit step values for this check: `tools/reference.js` quantises them as the core stores them.
- The device build has the same level as the prototype (within 0.002 dB). Its spectrum is within 0.01 to 0.5 dB on average.

**CPU** on a 2.3 GHz Xeon, device build:
- The heaviest factory pattern needs about 510 ns per sample, which is 2.2 % of a core.
- Effects and master alone cost 78 ns.
- Per engine, with a hit on every 1/16: Moln 156 ns, Stöt 95, Puls 76, Rost 47, Gnista 34, Skärva 14, Klang 12.

The FM-1 (pi32v2 at 360 MHz) has to be measured on the device.

**The prototype was changed to match the hardware:** its delay line is now 32768 samples per channel, the same as here.

## Decisions made in the port

- **Step timing** uses a 64-bit fixed-point clock with 1/65536-sample resolution.
  - In the device build, an even and an odd step always add up to exactly two steps. Swing is applied at 30-bit precision, so tracks never drift apart.
  - Nudge and straight ratchets are worked out in fixed point too.
- **The sampler's read position** is an integer plus a fraction. A single float only has 1/128-sample resolution deep into a 7 s slot.
- **Feedback paths** get a 1e-20 DC offset, and decaying followers snap to zero, so nothing turns into slow denormal numbers.
- **Generated data** is rounded the way JavaScript's `Math.round` rounds, not with Python's `round`, which rounds halves to even.

## Next

1. **Try it on an FM-1:** the knobs' feel, the LEDs, the flash writes.
2. **Measure CPU on the device.** If it is short:
   - Moln and Stöt filter coefficients every 16 samples instead of every sample.
   - Tracks split across both cores.

## Licence

GPL-3.0-only (LICENSE). `third_party/fastmath.h` is fm1-base's, under MPL-2.0 (third_party/LICENSE-MPL-2.0), which may be combined with GPL code.
