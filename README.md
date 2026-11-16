# Nightshift Drums

**Download:** it comes with NightShift's free installers for Windows and Mac, on the
[NightShift releases page](https://github.com/michaeldedmondson-design/nightshift/releases/latest).
Free software under the GNU GPL v3 or later ([LICENSE](LICENSE)).

A multi-mic acoustic drum plugin (VST3 on Windows; VST3, AU and a standalone app on a Mac) in the
Nightshift family, built on JUCE. It plays the three free studio kits from
[DrumGizmo.org](https://drumgizmo.org) with every microphone and every recorded hit, the way
Superior Drummer plays its kits:

| Kit | What it is | Download |
| --- | --- | --- |
| **Crocell** | the Danish metal band Crocell's kit: double pedal, 2 rack + 2 floor toms, 3 crashes, 2 chinas, 2 splashes; 15 mics | 5.3 GB |
| **DRS** | a handmade DRS Drums kit with Paiste cymbals, recorded with sticks and with brushes; 13 mics | 2.6 GB |
| **Muldjord** | Lars Muldjord's Tama Superstar: 2 kicks, 4 toms, 2 rides; 16 mics | 2.4 GB |

- **Every mic, every hit.** Each hit sounds on all of the kit's mics at once (close mics, overheads,
  room), with the real bleed between them. A note picks one of the instrument's recorded hits by how
  hard it's played, never the same one twice running (no machine-gunning).
- **A mixer inside.** A strip per mic: level, pan, mute, solo, polarity, and an output: the main out
  or one of Out 2..9, so a mic can have a channel of its own in the host. Each kit opens with its own
  default mix, worked out from its recordings (where each drum sits between the overheads, the close
  mics against them, the mics that are the other way round). **Bleed** sets how much each drum is
  heard in the other drums' close mics; **Dynamics** how far velocity moves the loudness.
- **The kit as it looks.** The Kit page is a picture of the very kit you hear (made with an image AI
  from a description of the real kit; the prompts are in `docs/kit-picture-prompts.md`). Every piece
  lights when it plays and plays when clicked:
  the hi-hat opens at its edge, the snare's rim is a rimshot, the ride's bell is its bell;
  right-click for every articulation. Shift-click for a softer hit.
- **Hi-hat.** Closed, open, half open, pedal; a hit stops the hi-hat ringing. With an electronic
  kit's hi-hat pedal (MIDI CC 4), the hi-hat note plays closed, half open or open as the pedal is.
  Aftertouch on a cymbal's note chokes it.
- **Grooves.** NightShift's offline AI drummer (19 styles: metalcore to neo-soul) writes a groove
  for a style or for words ("funk, busy hats, 96 bpm"): sections, fills and its feel. It plays on the
  kit in time with the host, and drags into the song as MIDI. Nothing goes to the internet.
- **General MIDI notes** for the main drums (36 kick, 38 snare, 42/44/46 hi-hat, 49/57 crashes,
  51/53 ride, 50/48/47/45/43/41 toms), so NightShift's drummer and any GM drum part play the right
  drums. The extras (stopped crashes, a second china, brushes' sweep...) are on notes General MIDI
  leaves free; hover a piece on the Kit page to see its note.

## Getting the kits

The Kits page gets them: it downloads a kit's zip from drumgizmo.org (carrying on where it stopped
if a download is interrupted), checks it against drumgizmo.org's MD5, sets it up (every hit of every
mic into one file Nightshift Drums streams from, at 24 bits) and deletes the zip. Kits live in
`%LOCALAPPDATA%\Programs\Nightshift Drums\Kits` (a Mac: `~/Library/Application Support/Nightshift
Drums/Kits`); Change folder moves where it looks.

Each hit's first tenth of a second is loaded into memory; the rest streams from disk, a few chunks
ahead of every voice, on a thread of its own. The audio thread never waits for the disk.

## Building

Needs Visual Studio's C++ build tools (with CMake) and the Nightshift checkout next door, whose JUCE
this project uses (`-DJUCE_DIR=...` to point elsewhere).

```bash
build.cmd Release
```

Builds the plugin, its standalone app and `drumstool`, and copies the `.vst3` to
`%LOCALAPPDATA%\Programs\Common\VST3` (beside the Lofi and Bass plugins). `install.cmd` also puts the
standalone app in `%LOCALAPPDATA%\Programs\Nightshift Drums` with a Start menu shortcut (it plays from a
MIDI keyboard or e-kit through the MiniFuse's ASIO driver: its Options menu). `build_clang.cmd` builds
`drumstool` with clang-cl, the compiler family Apple's Xcode uses; `mac/build.sh` is the Mac build.

## Checking it

```bash
drumstool --test                                   # every test (or --test <name>)
drumstool --import <zip> <crocell|drs|muldjord> <folder>
drumstool --info <kits folder> <kit>               # mics, default mix, hits
drumstool --render <kits folder> <kit> out.wav --groove "rock" [--rate 44100] [--bpm 110] [--seconds 16]
drumstool --stress <kits folder> <kit> [--seconds 20] [--block 128]
drumstool --snapshot out.png --kits <folder> --kit <kit> --page kit|mixer|grooves|kits [--groove "..."] [--hit 36,38] [--textaudit]
drumstool --vst3 "%LOCALAPPDATA%\Programs\Common\VST3\Nightshift Drums.vst3"
```

`--info <kits> <kit> --balance` also shows where the overheads and room hear each instrument.
SPEC.md has every number, and what each kit measured.

The tests make a small DrumGizmo kit of their own and check the importer (every sample back within a
24-bit step), the engine (a note plays its hit sample for sample; velocity, no repeats, bleed, the
hi-hat choke, streaming identical to reading the file), the resampler, the plugin (notes on their
sample, the same output at any block size and host rate, the mixer, sessions), the kit library, the
three kits' note maps and the grooves. `--stress` plays blast beats with every cymbal in real time
and reports the CPU and any frame the disk delivered late (there must be none).

`--snapshot --textaudit` lists any text that's under 15 px, squeezed, cut short, clipped or faint
(there must be none). The standalone app takes `--snapshot out.png --page <page> --textaudit` too.

## Layout

- `engine/` – the kit format (`KitFile`), the DrumGizmo importer, the three kits' definitions
  (`Catalog`: pieces, articulations, notes, where each piece is in its picture), the engine and its
  streamer, the resampler, the mixer, and the library (downloads).
- `groove/` – NightShift's offline drummer (`OfflineDrummer`, `Request`: copied as they are from
  NightShift's `src/ai`) and grooves as MIDI (`Grooves`).
- `plugin/` – the processor, the window and its pages, the kit view.
- `common/` – the look (`Theme`), the scaled window, the text check (a copy of NightShift's).
- `resources/kits/` – the kits' pictures.
- `tools/` – `drumstool` and the standalone app.

## Licences

Built on JUCE (AGPLv3 / commercial). The kits are DrumGizmo.org's under Creative Commons
Attribution 4.0: CrocellKit 1.1 (sampled by Lars Muldjord; kit set up by Crocell, mics by JBOSound),
DRSKit 2.1 (DrumGizmo.org with DRS Drums; sampled by Deva and Lars Muldjord), MuldjordKit 3 (Lars
Muldjord). "Drum samples provided by DrumGizmo.org." The pictures of the three kits were generated
with an image AI for Nightshift Drums, from descriptions of the real kits.
