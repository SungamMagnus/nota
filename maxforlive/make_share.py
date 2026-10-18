#!/usr/bin/env python3
"""Build the shareable Nóta devices: python3 make_share.py

Produces dist/Nota-for-Live/ and dist/Nota-for-Live.zip with two self-contained (frozen) .amxd files and a short README.
Frozen = the nota~ external and the scripts are embedded in the device, so the people you share it with install nothing else.

Nothing in the files depends on the machine they were built on, and the external contains no trained model.
"""
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).parent
BUILD = HERE / "build"
DIST = HERE / "dist" / "Nota-for-Live"
sys.path.insert(0, str(HERE))
import freeze  # noqa: E402

README = """Nóta for Ableton Live
=====================

Monophonic audio to MIDI for bass and voice, with chord, flourish and humanize tools. Two Max for Live devices:

  Nóta source        Audio Effect. Put it on the audio track with your bass or voice. It turns the audio into notes.
  Nóta destination   MIDI Effect. Put it on a MIDI track, before an instrument. It plays the notes (and can add chords).

Install
  1. Drag both .amxd files into Live (or into your User Library: Presets > Audio Effects > Max Audio Effect, and
     MIDI Effects > Max MIDI Effect).
  2. Put Nóta source on the audio track, Nóta destination on a MIDI track with an instrument after it.
  3. Give both devices the same Channel Link number (default 1). Use different numbers for several pairs.
  4. Play. The destination shows the incoming note.

Needs
  Ableton Live 11 or 12 with Max for Live, on a Mac (Intel or Apple silicon). Nothing else to install: everything the
  devices need is inside the two files.

If macOS says the device cannot be opened
  The devices are not notarized. Open System Settings > Privacy & Security and choose "Allow Anyway", or run
      xattr -dr com.apple.quarantine "Nóta source.amxd" "Nóta destination.amxd"
  in Terminal and open them again.

Notes
  * Hover a control in Live (Info View) for a one-line explanation.
"""


def run(*cmd):
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    run(sys.executable, HERE / "make_devices.py")
    if not (BUILD / "CMakeCache.txt").exists():
        run("cmake", "-S", HERE, "-B", BUILD)
    run("cmake", "--build", BUILD, "-j")
    mxo = HERE / "externals" / "nota~.mxo"
    run("codesign", "--force", "--sign", "-", mxo)   # ad-hoc: Apple silicon refuses unsigned code

    if DIST.parent.exists():
        shutil.rmtree(DIST.parent)
    DIST.mkdir(parents=True)
    js = HERE / "package" / "nota" / "javascript"
    freeze.freeze(HERE / "devices" / "Nóta source.amxd", DIST / "Nóta source.amxd", [mxo], [js / "nota_info.js", js / "nota_scope.js"])
    freeze.freeze(HERE / "devices" / "Nóta destination.amxd", DIST / "Nóta destination.amxd", [], [js / "nota_dest.js"])
    (DIST / "README.txt").write_text(README, encoding="utf-8")

    zpath = DIST.parent / "Nota-for-Live.zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(DIST.iterdir()):
            z.write(f, f"{DIST.name}/{f.name}")
    for f in sorted(DIST.iterdir()):
        print(f"  {f.name}: {f.stat().st_size} bytes")
    print(f"{zpath}: {zpath.stat().st_size} bytes")


if __name__ == "__main__":
    main()
