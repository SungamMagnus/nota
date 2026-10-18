#!/usr/bin/env python3
"""Build the shareable Nóta devices: python3 make_share.py

Produces dist/Nota-for-Live/ and dist/Nota-for-Live.zip with two self-contained (frozen) .amxd files and a short README.
Frozen = the nota~ external and the scripts are embedded in the device, so the people you share it with install nothing else.

The external is built WITHOUT the trained neural weights (NOTA_NEURAL=OFF): nothing derived from the training recordings is
in the files, and the AI Assist control is left out of the share variant of the source device.
"""
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).parent
BUILD = HERE / "build-share"
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
  * This version does not include the experimental AI Assist.
"""


def run(*cmd):
    subprocess.run([str(c) for c in cmd], check=True)


def main():
    run(sys.executable, HERE / "make_devices.py", "--share")
    if not (BUILD / "CMakeCache.txt").exists():
        sdk = HERE / "build" / "_deps" / "max-sdk-base-src"
        extra = [f"-DFETCHCONTENT_SOURCE_DIR_MAX-SDK-BASE={sdk}"] if sdk.exists() else []
        run("cmake", "-S", HERE, "-B", BUILD, "-DNOTA_NEURAL=OFF", f"-DC74_LIBRARY_OUTPUT_DIRECTORY={BUILD / 'externals'}", *extra)
    run("cmake", "--build", BUILD, "-j")
    mxo = BUILD / "externals" / "nota~.mxo"
    exe = mxo / "Contents" / "MacOS" / "nota~"
    if exe.stat().st_size > 800_000:
        sys.exit(f"{exe} is {exe.stat().st_size} bytes: it looks like it still contains the neural weights (NOTA_NEURAL=OFF?)")
    run("codesign", "--force", "--sign", "-", mxo)   # ad-hoc: Apple silicon refuses unsigned code

    if DIST.parent.exists():
        shutil.rmtree(DIST.parent)
    DIST.mkdir(parents=True)
    js = HERE / "package" / "nota" / "javascript"
    freeze.freeze(HERE / "devices-share" / "Nóta source.amxd", DIST / "Nóta source.amxd", [mxo], [js / "nota_info.js", js / "nota_scope.js"])
    freeze.freeze(HERE / "devices-share" / "Nóta destination.amxd", DIST / "Nóta destination.amxd", [], [js / "nota_dest.js"])
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
