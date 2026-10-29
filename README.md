# Beads Textures (VST3 prototype)

A plugin port of the open-source Mutable Instruments **Beads** granular
texture synthesizer. The audio engine is the original Beads DSP code
(MIT licence); everything around it (plugin shell, knobs) is new.

## Getting the plugin (no programming needed)

1. Make a free account on github.com and create a new empty repository.
2. Upload the contents of this folder to it (the "Add file > Upload files"
   button works; keep the `.github` folder).
3. Open the **Actions** tab. The "Build plugin" job starts by itself
   (about 10-15 minutes).
4. When it is green, open it and download `BeadsTextures-Windows`
   (or macOS / Linux) from the *Artifacts* list at the bottom.
5. Unzip and copy the `Beads Textures.vst3` folder to:
   * Windows: `C:\Program Files\Common Files\VST3`
   * macOS: `/Library/Audio/Plug-Ins/VST3` (first start: right-click >
     Open, or run `xattr -cr "Beads Textures.vst3"` in Terminal, because it
     is not code-signed)
   * Linux: `~/.vst3`
6. Rescan plugins in your DAW. A standalone app is built too.

## How to use it (short)

* Put it on a track as an effect (audio in -> audio out).
* **SEED LOCK** (on by default) = grains play continuously, like the
  lit SEED button on the module. Turn it off and use **SEED (hold)** to
  fire grains by hand.
* TIME = where in the recorded buffer grains are taken from, SIZE = grain
  length (fully clockwise = delay mode), SHAPE = grain envelope,
  PITCH = transposition (snaps to musical intervals like the module).
* The small knob under each is the "attenurandomizer": centre = nothing,
  one side = smooth random drift / randomisation, other side = random
  amount.
* DENSITY: left = slow regular grains, centre = tap with SEED, right =
  random grains.
* FREEZE holds the recorded buffer. QUALITY switches between the four
  module sound modes. MONO doubles recording time.
* "MIDI = SEED GATE": MIDI notes act like a gate/clock into the SEED jack.

## For developers

`core/` is plain C++ with no plugin-framework types (reusable for hardware
later). `tools/render_test.cc` renders test audio offline.
Local build: `cmake -B build && cmake --build build --config Release`.
