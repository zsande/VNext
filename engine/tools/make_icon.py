#!/usr/bin/env python3
"""Makes the Windows icon, build.nosync/icon.ico (what the .exe shows in Explorer and the taskbar), from the
player's icon, build.nosync/icon.png: its sizes made with macOS's sips, each kept as a PNG in the .ico. Run it
on a Mac (python3 engine/tools/make_icon.py) when icon.png changes."""
import pathlib
import struct
import subprocess
import tempfile

BUILD = pathlib.Path(__file__).resolve().parent.parent / "build.nosync"
SIZES = (16, 24, 32, 48, 64, 128, 256)

pictures = []
with tempfile.TemporaryDirectory() as folder:  # (the sizes, made here and read back)
    for size in SIZES:
        out = pathlib.Path(folder) / f"{size}.png"
        subprocess.run(["sips", "-z", str(size), str(size), str(BUILD / "icon.png"), "--out", str(out)], check=True, capture_output=True)
        pictures.append(out.read_bytes())

# An .ico: its header (an icon, so many pictures), then each picture's entry (its size, 256 written as 0; 32
# bits a pixel; where its PNG is), then the PNGs.
at = 6 + 16 * len(SIZES)
entries, data = b"", b""
for size, png in zip(SIZES, pictures):
    entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), at + len(data))
    data += png
(BUILD / "icon.ico").write_bytes(struct.pack("<HHH", 0, 1, len(SIZES)) + entries + data)
print(f"Made {BUILD / 'icon.ico'}: {len(SIZES)} sizes, {6 + len(entries) + len(data)} bytes.")
