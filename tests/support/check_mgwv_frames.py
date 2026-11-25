#!/usr/bin/env python3
"""
Checks the time history of a web view file (MGWV) written by openmodel-cli.

usage: check_mgwv_frames.py FILE MIN_FRAMES DEFAULT_FRAME

Reads the JSON descriptor (little-endian: magic at 0, JSON size at byte 12, JSON from byte 24) and verifies that
it holds at least MIN_FRAMES frames with strictly increasing times, that every frame has its own chunk inside the
binary part, and that the default frame index is DEFAULT_FRAME. Exit status 0 on success.
"""
import json
import struct
import sys


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    path, min_frames, default_frame = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    with open(path, "rb") as f:
        data = f.read()
    json_size = struct.unpack_from("<I", data, 12)[0]
    desc = json.loads(data[24:24 + json_size].decode("utf-8"))
    frames = desc.get("frames", [])
    if len(frames) < min_frames:
        print(f"FAIL: {len(frames)} frames, expected at least {min_frames}")
        return 1
    times = [f["time"] for f in frames]
    if any(b <= a for a, b in zip(times, times[1:])):
        print(f"FAIL: frame times are not strictly increasing: {times}")
        return 1
    binary_size = len(data) - 24 - json_size
    for f in frames:
        if f["offset"] < 0 or f["offset"] + 12 * desc["counts"]["vertices"] > binary_size:
            print(f"FAIL: frame '{f['label']}' lies outside the binary part")
            return 1
    if desc.get("frame") != default_frame:
        print(f"FAIL: default frame is {desc.get('frame')}, expected {default_frame}")
        return 1
    print(f"OK: {len(frames)} frames, times {times}, default frame {default_frame}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
