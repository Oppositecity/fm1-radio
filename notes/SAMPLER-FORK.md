# Radio, sampler fork: the spec

A fork of [charlesvestal/fm1-radio](https://github.com/charlesvestal/fm1-radio) 1.3.1. AAC stays. The synth
becomes a chromatic sampler with banks kept in flash, and the 11 black keys on Home get glitch effects.

## Budget, as found

| | Upstream 1.3.1 | This fork |
|---|---|---|
| Code (XIP slot) | 12 bytes spare | the new code is paid for by dropping FONT_XS (~7.3 KB of data): the knob labels and the password keys use FONT_S. On x86 -Os the fork is ~3.7 KB smaller than upstream; the device build is the real measure |
| Heap, HE-AAC station | ~18 KB spare | +1.75 KB (the comb's line 1 KB, the sampler's page 256 B, shortwave's line 512 B) |
| FX history | 12 KB, int16 at 22 kHz, ~280 ms | same 12 KB, mu-law 8-bit at 22 kHz, **~557 ms** |
| Sample storage | none | **XDATA1 (92 KB) + XDATA0 (16 KB)**, flash windows Radio never used |

Keeping AAC rules out a bigger RAM buffer. Samples therefore live in flash and play in place (`plat_data`), as
LOOM-8's do. RAM only stages them.

## 1. The history goes mu-law

`fx_hist` changes from `int16_t[6144]` to `uint8_t[12288]` of G.711 mu-law. It's the same RAM and twice the time.
- The stutters, tape stop and reverse get ~557 ms instead of ~280 ms. 1/8 stutter now holds down to ~54
  BPM (was ~108).
- That history is also what GRAB copies from.

## 2. The sampler (ARP), replacing the synth

The 4 voices, the attack/release envelope, voice stealing, the 27-key layout and OCT -/+ are kept from synth.c.
The oscillator, polyBLEP, wave morph, filter and tanh go. Each voice reads the slot's mu-law samples from flash at a
varispeed rate: key C4 plays it as recorded, and each key up or down is a semitone (Mirage/SK-1 style: higher is
shorter).

| Control | Sampler page |
|---|---|
| keys | play the slot chromatically (C4 = as recorded) |
| **REC** (hold) | record the radio live into the slot, until let go or full |
| **FX** (tap) | GRAB: the last ~0.55 s of what you heard, into the slot (retroactive) |
| **SELECT** | slot 1..4 |
| **SEL** | loop on / off |
| **KNOB 1** | start |
| **KNOB 2** | length (from start) |
| **KNOB 3** | attack |
| **KNOB 4** | release |
| **OCT -/+** | two octaves each way |
| **PLAY** | the radio, play / stop (so you can sample without leaving) |
| **EDIT** | reverse on / off |
| **ENV** | poly / chord (a major triad a key) / glide (1 voice, ~40 ms slide; letting go glides back to the highest held) |
| **LFO** | arpeggio: held keys low to high, a 1/16 each at the tap tempo, half-step gate (legato when gliding) |
| **SEQ** | route: dry / resample (the sampler into the history, so GRAB takes it) / thru FX (through the Home effects) |
| **HOME** (hold) | the sampler's controls page; letting go comes back here |
| **HOME** (tap, on letting go) / **ARP** | back |

| **PRESETS** | the radio's level, 0-100% in 5% steps (squared, ~45 ms smoothing), before the effects and the history (GRAB takes what was heard); on every page (Home shows it after the bit rate when below 100%), kept in OBJ_RADIO's spare byte (`rset.rdown`, steps below full: older saves load at 100%) 5 s after a change |
| **ALGORITHM** | the keys' level, the same way (after the sampler's soft limit, before the route): on every page, shown in the title when below 100%, kept in OBJ_RADIO's `playing` byte above bit 0 (steps below full; older saves: 0, full) |

The modes live in `smp_set_t.fl` (the struct's spare byte, so the saved settings keep their layout). The piano
picture is gone from this page: its place and the hint row show the buttons as the FM-1 has them, two rows (FX SEL
ENV LFO EDIT / HOME SEQ PLAY REC), each its name and under it what it does or its mode, teal when on.

### Slots: the "save"

Every capture is written straight to flash, so it's saved: it survives power-off and a firmware update of this
fork. No separate save step is needed.

| Slot | Where | Size | At 11 kHz |
|---|---|---|---|
| 1 | XDATA1 sectors 0-6 | 28 KB | ~2.6 s |
| 2 | XDATA1 sectors 7-13 | 28 KB | ~2.6 s |
| 3 | XDATA1 sectors 14-20 | 28 KB | ~2.6 s |
| 4 | XDATA0 sectors 0-3 | 16 KB | ~1.5 s |

- **Rate:** slots store 8-bit mu-law at 11.025 kHz (every second history sample). That's AM-radio bandwidth,
  SK-1 territory, and 2x the length of 22 kHz. Change `SMP_DIV` to 1 for 22 kHz and half the time.
- **Slot lengths and settings** live in OBJ_RADIO (where the synth's settings were).
- **Loader safety** (plat.h): in these windows bytes 0xF06..0xF07 of a sector must never read "AT". The writer
  changes a 'T' there to 'U' (one sample, inaudible).
- **Erase:** REC erases the whole slot first (7 x ~40 ms: a ~0.3 s gap, then it records). GRAB erases only what it
  needs (2 sectors) and freezes the history meanwhile, so the grabbed window is exactly what was heard.
- **REC writing:** the main loop follows the history's write index and programs a 256-byte page whenever one is
  full (~43 a second). There's no extra RAM and ~0.5 s of slack.
- Another firmware that uses XDATA (LOOM-8) wipes the slots. So does the stock firmware's install.

## 3. Glitch effects on the black keys (Home)

Black keys are bits 16..26 of `fx_held`. Sources (one at a time, the lowest held wins, as before) read the history.
"Live" sources keep the history writing.

| Black key | Effect | Kind |
|---|---|---|
| F#3 | **scatter**: random 30-90 ms grains from anywhere in the history | source |
| G#3 | **skip**: a CD skipping, every 1/16 jumps back 0-3 sixteenths | source |
| A#3 | **scratch**: the head rocks back and forth | source |
| C#4 | **rise**: 1/16 stutter, each repeat a step higher | source |
| D#4 | **warble**: tape wow and flutter (a moving delay) | live source |
| F#4 | **glitch**: tiny random loops (5-40 ms), new each 1/16 | source |
| G#4 | **bitflip**: bursts of flipped bits in the 16-bit sample | post |
| A#4 | **fold**: wavefolder | post |
| C#5 | **comb**: a resonator at D3, the radio as a string | post (1 KB) |
| D#5 | **shortwave**: multipath (the radio plus itself 0.2-4.5 ms later, drifting: sweeping notches) and ionospheric fading; only the radio, nothing synthesized | post (512 B) |
| F#5 | **gate**: a 16-step trance gate on the tap tempo | post |

Also on Home (FX bits 27..29, held buttons; key 15 changed):

| Control | Effect | Kind |
|---|---|---|
| white 14 | **half speed**: now two crossfaded 93 ms grains read behind the live history at half speed: the radio goes on, an octave down (was: the last ~557 ms looped) | live source |
| white 15 | **octave up** (was mute): the same at twice the speed | live source |
| **LFO** | **echo**: the history 1/8 and 1/4 note back (halved while longer than the history) | post |
| **EDIT** | **sweep**: a resonant low-pass, 210 Hz to ~5 kHz and back every 2 beats, from the press | post |
| **SEQ** | **pump**: ducked to 0.15 on each beat, back by the next (cubic), from the press | post (gain) |
| **ENV** | **latch**: what is held when pressed stays on (on every page) until ENV again | - |

## 4. If it doesn't fit (the order to cut)

FONT_XS is already gone (section 1's table). The device build is the only measure. If it's still over:
1. Drop the dump's place/list/fav lines (`radio_ui_dump`). They're debug only, and `radio_ctl.py dump` keeps the
   decoder/loop rate line.
2. Drop effects from the bottom of the table up. Each is roughly 60-200 bytes.
3. Last resort: the HOME-held help page.

## 5. Safety

- App-level only: nothing in base/ (boot, loader, install, supplies) changes.
- Writes go only to XDATA0/XDATA1, through `plat_data_*`, which refuses anything outside the windows.
- Output stays clamped to full scale (as upstream).
- Never `--force`. Keep the stock firmware and radio-1.3.1 `.fwsc` on disk.

## Open, for the device

- Do ~43 page programs a second during REC click the audio? If so, set `SMP_DIV` 2 to 4 (fewer writes), or mute
  REC's monitor.
- Main-loop rate and underruns with 4 voices reading flash plus HE-AAC (`radio_ctl.py dump`).

## Tests

- `CC=clang sh tests/run_tests.sh`: upstream's, plus `tests/sampler_test.c` (pitch, loop, window, voices, mu-law).
  On Linux use clang: gcc's c99 inline rules break the AAC decoder's host link.
- `sh tests/run_local.sh`: a RADIO_LOCAL_TEST host build with a generated test MP3 as the station. It plays the
  white and black FX, GRABs into slot 1, RECs slot 2 until full, plays both chromatically, checks the slots
  survive a reboot, and writes WAVs and screens to app/build/local/.
- Two upstream test-only fixes rode along (`#ifdef RADIO_LOCAL_TEST`, not in a firmware): the built-in MP3 path
  now makes its decoder, and the places fetch doesn't stop it.
