#!/usr/bin/env python3
"""Writes a washer (a ring with a bore) and half a washer, as hexahedra and as tetrahedra with mixed winding.

The meshes test the recognition of holes in the viewer. The bore is a cylinder of radius R_IN made of planar facets 15 degrees
apart, so the size of the wall is known from the construction; the outer wall has the same facets but is convex (material inside),
and must not be taken for a hole.

Output, in <outdir>:
  washer.inp / washer_tet.inp            the closed ring (C3D8 / C3D4)
  washer_half.inp / washer_half_tet.inp  the half ring from 0 to 180 degrees: its bore is half a hole, cut by a symmetry plane

usage: make_washer_mesh.py <outdir>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from annular_mesh import build_sector, split_tets, write_inp   # noqa: E402

R_IN, R_OUT, HEIGHT = 3.0, 6.0, 2.0
STEP_DEG = 15.0
R_DIV, Z_DIV = 2, 2


def main(outdir):
    for name, span in (("washer", 360.0), ("washer_half", 180.0)):
        coords, hexes = build_sector(R_IN, R_OUT, HEIGHT, round(span / STEP_DEG), R_DIV, Z_DIV, span)
        write_inp(os.path.join(outdir, name + ".inp"), coords, hexes, "C3D8", "Washer for hole-recognition tests", "WASHER")
        tets = split_tets(coords, hexes, 20261114)
        write_inp(os.path.join(outdir, name + "_tet.inp"), coords, tets, "C3D4", "Washer for hole-recognition tests", "WASHER")
        print(f"{name}: {len(coords)} nodes, {len(hexes)} hexahedra, {len(tets)} tetrahedra, bore facets every {STEP_DEG:g} degrees")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make_washer_mesh.py <outdir>")
    main(sys.argv[1])
