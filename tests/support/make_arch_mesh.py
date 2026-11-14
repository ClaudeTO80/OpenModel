#!/usr/bin/env python3
"""Writes a hexahedral annular-sector mesh (an "arch") with a finely faceted outer cylinder.

The mesh exists to test the selection of faces by normal in the viewer. Its outer surface is a cylinder
approximated by planar facets whose normals differ by exactly STEP_DEG degrees from one facet to the next, so
the expected size of a selection is known analytically:

  * the top and bottom faces (z = const) are planes made of THETA_DIV x R_DIV quadrilaterals;
  * the outer surface is THETA_DIV columns x Z_DIV rows of quadrilaterals; a selection that keeps the normal
    within A degrees of the clicked face reaches the columns whose centre is at most A degrees away (an
    angle against the clicked face), while a selection that follows the surface chain by chain (each step at
    most A degrees from the previous face) runs over every column as soon as A >= STEP_DEG.

Output, in <outdir>:
  arch.inp       Abaqus/CalculiX mesh, C3D8 elements, node ids starting at 1
  arch_tet.inp   the same geometry split into C3D4 tetrahedra. Every tetrahedron keeps a positive volume but gets a
                 random even permutation of its nodes (fixed seed), so which local face of an element lies on the
                 skin, and therefore the winding of the exported skin triangles, varies from one triangle to the
                 next inside one planar face, as it does in an unstructured mesh.

usage: make_arch_mesh.py <outdir>
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from annular_mesh import build_sector, split_tets, write_inp   # noqa: E402

R_IN, R_OUT, HEIGHT = 4.0, 5.0, 2.0
THETA_DIV, R_DIV, Z_DIV = 45, 2, 2
SPAN_DEG = 90.0
STEP_DEG = SPAN_DEG / THETA_DIV   # 2 degrees between the normals of neighbouring outer facets


def main(outdir):
    coords, hexes = build_sector(R_IN, R_OUT, HEIGHT, THETA_DIV, R_DIV, Z_DIV, SPAN_DEG)
    write_inp(os.path.join(outdir, "arch.inp"), coords, hexes, "C3D8", "Annular sector for face-selection tests", "ARCH")
    tets = split_tets(coords, hexes, 20261113)
    write_inp(os.path.join(outdir, "arch_tet.inp"), coords, tets, "C3D4", "Annular sector for face-selection tests", "ARCH")
    print(f"wrote {len(coords)} nodes, {len(hexes)} hexahedra and {len(tets)} tetrahedra, outer facets every {STEP_DEG:g} degrees to {outdir}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make_arch_mesh.py <outdir>")
    main(sys.argv[1])
