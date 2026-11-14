"""Hexahedral annular sectors and their tetrahedral split, shared by the mesh generators of the viewer tests.

  build_sector()  nodes and C3D8 elements of a ring (or a part of one) around the z axis, with `theta_div` columns, `r_div` rings and
                  `z_div` layers; a span of 360 degrees closes the ring (the last column shares its nodes with the first)
  split_tets()    the same geometry as C3D4 elements: each hexahedron is cut into the 6 tetrahedra of the Kuhn split (conforming
                  between neighbouring cells), and every tetrahedron gets a random even permutation of its nodes (fixed seed),
                  which keeps its volume positive but makes the winding of the exported skin triangles vary from one triangle to
                  the next, as in an unstructured mesh
  write_inp()     Abaqus/CalculiX file with one element type
"""
import itertools
import math
import os
import random


def build_sector(r_in, r_out, height, theta_div, r_div, z_div, span_deg):
    """Returns (coords, hexes): coords[i] is the position of node i + 1, hexes[e] the 8 node ids (1-based) of element e + 1."""
    closed = abs(span_deg - 360.0) < 1e-9
    node_id = {}
    coords = []

    def node(i, j, k):
        key = (i, j % theta_div if closed else j, k)
        if key not in node_id:
            r = r_in + (r_out - r_in) * i / r_div
            theta = math.radians(span_deg * key[1] / theta_div)
            coords.append((r * math.cos(theta), r * math.sin(theta), height * k / z_div))
            node_id[key] = len(coords)
        return node_id[key]

    hexes = []
    for k in range(z_div):
        for j in range(theta_div):
            for i in range(r_div):
                bottom = [node(i, j, k), node(i + 1, j, k), node(i + 1, j + 1, k), node(i, j + 1, k)]
                top = [node(i, j, k + 1), node(i + 1, j, k + 1), node(i + 1, j + 1, k + 1), node(i, j + 1, k + 1)]
                hexes.append(bottom + top)
    return coords, hexes


def split_tets(coords, hexes, seed):
    """Tetrahedra of the Kuhn split of every hexahedron, with a random even permutation of the nodes of each (see the module text)."""
    corner = {(0, 0, 0): 0, (1, 0, 0): 1, (1, 1, 0): 2, (0, 1, 0): 3, (0, 0, 1): 4, (1, 0, 1): 5, (1, 1, 1): 6, (0, 1, 1): 7}
    rng = random.Random(seed)
    even = [p for p in itertools.permutations(range(4)) if sum(1 for i in range(4) for j in range(i + 1, 4) if p[i] > p[j]) % 2 == 0]

    def volume(a, b, c, d):
        u = [coords[b - 1][i] - coords[a - 1][i] for i in range(3)]
        v = [coords[c - 1][i] - coords[a - 1][i] for i in range(3)]
        w = [coords[d - 1][i] - coords[a - 1][i] for i in range(3)]
        return (u[0] * (v[1] * w[2] - v[2] * w[1]) - u[1] * (v[0] * w[2] - v[2] * w[0]) + u[2] * (v[0] * w[1] - v[1] * w[0])) / 6.0

    tets = []
    for hexa in hexes:
        for axes in itertools.permutations(range(3)):
            path = [(0, 0, 0)]
            for axis in axes:
                step = list(path[-1]); step[axis] = 1; path.append(tuple(step))
            quad = [hexa[corner[c]] for c in path]
            if volume(*quad) < 0:
                quad[2], quad[3] = quad[3], quad[2]
            assert volume(*quad) > 0
            tets.append([quad[i] for i in rng.choice(even)])
    return tets


def write_inp(path, coords, connectivity, element_type, title, elset="PART"):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w") as f:
        f.write(f"*HEADING\n{title}\n*NODE\n")
        for n, (x, y, z) in enumerate(coords, start=1):
            f.write(f"{n}, {x:.9g}, {y:.9g}, {z:.9g}\n")
        f.write(f"*ELEMENT, TYPE={element_type}, ELSET={elset}\n")
        for e, conn in enumerate(connectivity, start=1):
            f.write(f"{e}, " + ", ".join(str(n) for n in conn) + "\n")
