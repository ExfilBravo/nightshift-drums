# Nightshift Drums: specification

What the plugin does, in numbers, and what `drumstool --test` and `--stress` check. Change a number
here when the code changes it.

## Kits

| | Crocell | DRS | Muldjord |
| --- | --- | --- | --- |
| Source | CrocellKit 1.1 (`CrocellKit_full.xml`) | DRSKit 2.1 (`DRSKit_full.xml`) | MuldjordKit 3 (`MuldjordKit3.xml`) |
| Zip | 5,646,502,341 B, MD5 fa2be0f8... | 2,803,397,710 B, MD5 8c4d4b61... | 2,536,576,789 B, MD5 8a66a3e9... |
| Rate | 48 kHz | 44.1 kHz | 44.1 kHz |
| Mics | 15 | 13 | 16 (one a trigger, off) |
| Picture | image AI, 2000 x 1116 | image AI, 2000 x 1116 | image AI, 2000 x 1116 |

- The importer reads straight from the zip (ZIP64 included) and keeps every instrument the kit file
  lists, every hit, every mic: frames of 24-bit samples, one per mic, each hit at its own scale
  (its peak = full scale), so quiet hits keep their detail. Back within half a 24-bit step of each
  hit's scale (and a float's rounding).
- A hit's level is the kit's own power figure, 10 log10(power), 0 dB = the instrument's hardest.
- An instrument's own mics: the ones its kit file calls main, and every overhead and room mic. The
  other close mics are bleed.
- Default mix (from each instrument's hardest hit, energy over its first 300 ms): overheads panned
  ±0.8 at 0 dB; room ±1.0 at -3 dB; trigger channels off. Each close mic belongs to the instrument
  whose hardest hit it hears loudest among those calling it their own: kick and snare in the middle,
  others where the overheads hear them (±0.7 at most); level so the instrument's close mics are
  kick +8, snare and toms +5, anything else 0 dB against the overheads (±12 dB at most). Two or more
  close mics on one drum: the loudest is the reference, another is flipped when the strongest
  correlation within ±3 ms is negative.

## Engine

- Voices: 96; at most 8 of one instrument (the oldest fades over 30 ms). With none free, the oldest
  (a fading one first) is taken.
- Velocity: target loudness = -max(the instrument's recorded range, 30 dB) × dynamics × (1 - v).
  It plays one of the 3 recorded hits nearest that, never one of the last two it played (when there's
  another), at gain target - hit level, -40..+6 dB.
- Groups (the hi-hat): a hit fades every voice of its group over 40 ms. "...Stopped" instruments fade
  their instrument over 30 ms. Aftertouch >= 64 on a note fades that note's instrument over 50 ms.
  All notes off / all sound off: everything over 10 ms.
- Bleed 0..200 % scales a hit's non-own mics; Dynamics 0..200 % scales the velocity range.
- Memory: each hit's first 0.1 s (all of a shorter one); the rest streams from the memory-mapped
  samples file into a ring per voice (4 chunks of 2048 frames), filled by a thread every 2 ms. The
  audio thread never waits: a frame not yet streamed plays as silence and counts as late. Offline
  renders (the host's non-real-time flag) read the file straight.
- Hi-hat pedal (CC 4, when on and once a CC 4 has arrived): a hi-hat Closed / Open / Half open note
  plays closed at 90 and up, half open from 40 (closed from 65 on a kit without half open), open below.

## Resampling and timing

- The engine runs at the kit's rate. At another host rate each mic goes through a 64-tap Kaiser
  (beta 8) windowed-sinc resampler, cut off at 0.91 of the lower rate's Nyquist, 512 phases with
  linear interpolation between them. A note sounds (33 / (kit rate / host rate)) host samples late:
  30 at a 48 kHz kit and 44.1 kHz host; that latency is reported to the host. At the kit's own rate:
  no resampling, no latency.
- A note lands on its sample: the output is the same, to within 1e-6, whatever the host's block size
  (512, 64, 333, 1 tested at 48, 44.1 and 96 kHz).

## Mixer

- A strip per mic (16 at most): level -60..+12 dB, constant-power pan, mute, solo (in place),
  polarity, output (Main, Out 2..9; a mic sent to an output the host has off goes to Main). Gains
  glide across a block when they change. Master: -60..+12 dB on the main out.

## Interface

- Designed at 1040 x 660 and shown at 1.25 (1300 x 825). Words at least 12 design px (15 px shown),
  small capitals 11.5 (14.4 px), contrast at least 4.5:1 (3:1 for inactive text).
  `--snapshot ... --textaudit` must report nothing on any page.

## Default mix orientation and level

- Overheads and room in the drummer's view. A kit whose drummer's-left cymbals (named ...L or
  ...left) are on average more than 2 dB louder in its "right" overhead than its right-hand ones is
  named from the audience's side: its overheads and room are turned round (Crocell is).
- The overheads' balance is measured against the snare's (a pair seldom hears evenly; Crocell's
  right overhead is about 4 dB hotter).
- A close mic is panned where its drum is in the kit's picture, as the drummer sits:
  (x / width - 0.5) x 2 x 0.75, kick and snare in the middle; the overhead estimate (balance
  against the snare x 0.15 per dB, at most ±0.7) only where the picture doesn't show its drum.
- Every fader of the default mix gets the kit's trim, so its hardest kick or snare (the first 50 ms,
  through the mix) peaks at -6 dBFS.

## Measured

From `drumstool --info`, `--render` and `--stress` on each kit (Windows, 48 kHz host, 2026-11-15):

| | Crocell | DRS | Muldjord |
| --- | --- | --- | --- |
| Hits / instruments | 758 / 27 | 541 / 35 | 302 / 17 |
| Recording | 51.6 min | 31.3 min | 22.9 min |
| Samples file (kit folder on disk) | 6.2 GB (6.3 GB) | 3.0 GB (3.1 GB) | 2.7 GB (2.8 GB) |
| In memory (0.1 s a hit) | 156 MB | 89 MB | 61 MB |
| Import from the zip | 44-64 s | 31-35 s | 21-28 s |
| Overheads named | from the audience (turned round) | as the drummer sits | as the drummer sits |
| Default mix trim | -2.5 dB | -5.3 dB | -6.1 dB |
| Rock at 110 bpm, peak / RMS | -6.1 / -28.6 dBFS | -4.8 / -29.8 dBFS | -5.1 / -27.4 dBFS |
| Stress, 64-sample blocks: CPU, busiest block of 1.33 ms, voices, late frames | 2.4 %, 0.19 ms, 34, 0 | 3.9 %, 0.38 ms, 35, 0 | 4.4 %, 0.59 ms, 35, 0 |

The stress test plays a dense metal groove with every cymbal, from a real-time thread as a host's
audio thread is (from an ordinary thread, the streamer can interrupt it, and blocks of 5-12 ms show up
that a host would never see). DRS and Muldjord are resampled from 44.1 kHz.

`drumstool --test`: 55 checks, all passing (48 on the test kit, 7 on the downloaded kits: every
articulation of every piece in each kit's picture sounds, DRS with sticks and with brushes).
`drumstool --vst3 <the .vst3>` loads the built plugin as a host would: it plays a snare and opens its window.
