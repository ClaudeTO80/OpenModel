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
import itertools
import math
import os
import random
import sys

R_IN, R_OUT, HEIGHT = 4.0, 5.0, 2.0
THETA_DIV, R_DIV, Z_DIV = 45, 2, 2
SPAN_DEG = 90.0
STEP_DEG = SPAN_DEG / THETA_DIV   # 2 degrees between the normals of neighbouring outer facets


def main(outdir):
    os.makedirs(outdir, exist_ok=True)
    node_id = {}
    coords = []

    def node(i, j, k):
        key = (i, j, k)
        if key not in node_id:
            r = R_IN + (R_OUT - R_IN) * i / R_DIV
            theta = math.radians(SPAN_DEG * j / THETA_DIV)
            coords.append((r * math.cos(theta), r * math.sin(theta), HEIGHT * k / Z_DIV))
            node_id[key] = len(coords)
        return node_id[key]

    elements = []
    for k in range(Z_DIV):
        for j in range(THETA_DIV):
            for i in range(R_DIV):
                bottom = [node(i, j, k), node(i + 1, j, k), node(i + 1, j + 1, k), node(i, j + 1, k)]
                top = [node(i, j, k + 1), node(i + 1, j, k + 1), node(i + 1, j + 1, k + 1), node(i, j + 1, k + 1)]
                elements.append(bottom + top)

    def write(name, element_type, connectivity):
        with open(os.path.join(outdir, name), "w") as f:
            f.write("*HEADING\nAnnular sector for face-selection tests\n*NODE\n")
            for n, (x, y, z) in enumerate(coords, start=1):
                f.write(f"{n}, {x:.9g}, {y:.9g}, {z:.9g}\n")
            f.write(f"*ELEMENT, TYPE={element_type}, ELSET=ARCH\n")
            for e, conn in enumerate(connectivity, start=1):
                f.write(f"{e}, " + ", ".join(str(n) for n in conn) + "\n")

    write("arch.inp", "C3D8", elements)

    # Tetrahedra: each hexahedron is cut along its main diagonal into the 6 tetrahedra of the Kuhn split, which
    # is conforming between neighbouring cells. The hexahedron corners in grid order are 0=(0,0,0) 1=(1,0,0)
    # 2=(1,1,0) 3=(0,1,0) 4=(0,0,1) 5=(1,0,1) 6=(1,1,1) 7=(0,1,1) in the cell's own (r, theta, z) axes.
    corner = {(0, 0, 0): 0, (1, 0, 0): 1, (1, 1, 0): 2, (0, 1, 0): 3, (0, 0, 1): 4, (1, 0, 1): 5, (1, 1, 1): 6, (0, 1, 1): 7}
    rng = random.Random(20261113)
    even = [p for p in itertools.permutations(range(4)) if sum(1 for i in range(4) for j in range(i + 1, 4) if p[i] > p[j]) % 2 == 0]

    def volume(a, b, c, d):
        u = [coords[b - 1][i] - coords[a - 1][i] for i in range(3)]
        v = [coords[c - 1][i] - coords[a - 1][i] for i in range(3)]
        w = [coords[d - 1][i] - coords[a - 1][i] for i in range(3)]
        return (u[0] * (v[1] * w[2] - v[2] * w[1]) - u[1] * (v[0] * w[2] - v[2] * w[0]) + u[2] * (v[0] * w[1] - v[1] * w[0])) / 6.0

    tets = []
    for hexa in elements:
        for axes in itertools.permutations(range(3)):
            path = [(0, 0, 0)]
            for axis in axes:
                step = list(path[-1]); step[axis] = 1; path.append(tuple(step))
            quad = [hexa[corner[c]] for c in path]
            if volume(*quad) < 0:
                quad[2], quad[3] = quad[3], quad[2]
            assert volume(*quad) > 0
            tets.append([quad[i] for i in rng.choice(even)])   # an even permutation keeps the volume positive
    write("arch_tet.inp", "C3D4", tets)
    print(f"wrote {len(coords)} nodes, {len(elements)} hexahedra and {len(tets)} tetrahedra, outer facets every {STEP_DEG:g} degrees to {outdir}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make_arch_mesh.py <outdir>")
    main(sys.argv[1])
