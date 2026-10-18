#!/usr/bin/env python3
"""Freeze a Max for Live device: embed the external(s) and scripts it needs into the .amxd itself, so the device is a single
self-contained file that works for anyone who opens it in Live (Max's "Freeze Device", done from the command line).

    freeze.py in.amxd out.amxd [--mxo path/to/name.mxo]... [--text path/to/script.js]...

Layout (read off devices frozen by Max): the unfrozen header, 'ptch' + LE length, then one 'mx@c' block holding the patcher
JSON followed by the embedded files, then a 'dlst' directory ('dire' entries: type, fnam, sz32, of32, vers, flag, mdat)
whose offsets are relative to the start of the 'mx@c' block. A .mxo bundle is stored as four entries:
name.mxo (4 bytes, type iLaX), name/zipfile (the bundle zipped), name/executable, name/plist.
"""
import argparse
import io
import json
import struct
import time
import zipfile
from pathlib import Path

MAC_EPOCH = 2082844800  # seconds from 1904-01-01 to 1970-01-01


def be32(n):
    return struct.pack(">I", n)


def pad4(b):
    """NUL padded to a multiple of four, with at least one NUL."""
    b += b"\0"
    return b + b"\0" * (-len(b) % 4)


def field(tag, payload):
    return tag + be32(8 + len(payload)) + payload


def dire(typ, name, size, offset, flag, mdat):
    body = (field(b"type", typ) + field(b"fnam", pad4(name.encode("utf-8"))) + field(b"sz32", be32(size))
            + field(b"of32", be32(offset)) + field(b"vers", be32(0)) + field(b"flag", be32(flag)) + field(b"mdat", be32(mdat)))
    return b"dire" + be32(8 + len(body)) + body


def zip_bundle(bundle):
    """The .mxo folder as a zip, entries named '<bundle name>/...'."""
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        root = bundle.name
        dirs = [bundle] + sorted(p for p in bundle.rglob("*") if p.is_dir())
        for d in dirs:
            zi = zipfile.ZipInfo(f"{root}/{d.relative_to(bundle).as_posix()}/".replace("/./", "/"))
            zi.external_attr = (0o40755 << 16) | 0x10
            z.writestr(zi, b"")
        for f in sorted(p for p in bundle.rglob("*") if p.is_file()):
            zi = zipfile.ZipInfo(f"{root}/{f.relative_to(bundle).as_posix()}")
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.external_attr = (0o100755 if "MacOS" in f.parts else 0o100644) << 16
            z.writestr(zi, f.read_bytes())
    return buf.getvalue()


def freeze(src, dst, mxos, texts):
    d = Path(src).read_bytes()
    assert d[:4] == b"ampf" and d[12:16] == b"meta" and d[24:28] == b"ptch", "not an unfrozen .amxd"
    kind = d[8:12]
    patcher = json.loads(d[32:].decode("utf-8"))

    deps = patcher["patcher"].setdefault("dependency_cache", [])
    for m in mxos:
        deps.append({"name": Path(m).name, "type": "iLaX"})
    for t in texts:
        deps.append({"name": Path(t).name, "type": "TEXT", "implicit": 1})
    main = (json.dumps(patcher, indent="\t") + "\n").encode("utf-8") + b"\0"

    now = int(time.time()) + MAC_EPOCH
    files = [(b"JSON", Path(dst).name, main, 0x11)]          # (type, name, data, flag)
    for m in mxos:
        m = Path(m)
        stem = m.stem
        exe = (m / "Contents" / "MacOS" / stem).read_bytes()
        plist = (m / "Contents" / "Info.plist").read_bytes()
        files.append((b"iLaX", m.name, b"\0\0\0\0", 0))
        files.append((b"\0\0\0\0", f"{stem}/zipfile", zip_bundle(m), 0))
        files.append((b"\0\0\0\0", f"{stem}/executable", exe, 0))
        files.append((b"\0\0\0\0", f"{stem}/plist", plist, 0))
    for t in texts:
        files.append((b"TEXT", Path(t).name, Path(t).read_bytes(), 0))

    region = bytearray()
    entries = b""
    off = 16
    for typ, name, data, flag in files:
        entries += dire(typ, name, len(data), off, flag, now)
        region += data
        off += len(data)
    block = b"mx@c" + be32(16) + be32(0) + be32(16 + len(region)) + bytes(region)
    dlst = b"dlst" + be32(8 + len(entries)) + entries
    payload = block + dlst
    out = b"ampf" + struct.pack("<I", 4) + kind + b"meta" + struct.pack("<I", 4) + struct.pack("<I", 7) \
        + b"ptch" + struct.pack("<I", len(payload)) + payload
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    Path(dst).write_bytes(out)
    return len(out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--mxo", action="append", default=[])
    ap.add_argument("--text", action="append", default=[])
    a = ap.parse_args()
    print(f"{a.dst}: {freeze(a.src, a.dst, a.mxo, a.text)} bytes")
