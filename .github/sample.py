# The samples at the end of a player (VNext.exe, VNext-Linux.AppImage), as engine/src/Sample.cpp reads it: the pack,
# its size in 8 bytes (little-endian), then "VNXSMPL1". The build workflow adds it to each release's player, and takes it
# off the players that go with the others (for Export the game: they're VNext's, not a game's).
#   python3 sample.py add PLAYER PACK     (one there already: replaced)
#   python3 sample.py strip PLAYER        (none, or a signed .exe with its certificates after it: left as it is)
import struct
import sys

MARK = b"VNXSMPL1"


def without(data):
    if data.endswith(MARK):
        size = struct.unpack("<Q", data[-16:-8])[0]
        if 0 < size <= len(data) - 16:
            return data[: len(data) - 16 - size]
    return data


what, player = sys.argv[1], sys.argv[2]
with open(player, "rb") as f:
    data = without(f.read())
if what == "add":
    with open(sys.argv[3], "rb") as f:
        pack = f.read()
    data += pack + struct.pack("<Q", len(pack)) + MARK
with open(player, "r+b") as f:  # (the same file, so it stays a program)
    f.write(data)
    f.truncate()
print(f"{player}: {len(data)} bytes, {'with' if data.endswith(MARK) else 'without'} the samples")
