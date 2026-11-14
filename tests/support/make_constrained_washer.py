#!/usr/bin/env python3
"""Adds `*BOUNDARY` blocks to a washer mesh written by make_washer_mesh.py, and writes what they must become.

The washer has a bore of radius 3, an outer radius of 6, a height of 2 and node rings at r = 3, 4.5, 6; nodes sit every 15 degrees at
z = 0, 1, 2. The constraints exercise every kind of symbol of the viewer:

  rim      all 24 nodes at r = 6, z = 0 (a node set): ux, uy, uz                  (three cones each)
  ux       the top node at r = 3, 0 deg: ux only                                  (a red cone pair)
  uy       the top node at r = 4.5, 270 deg: uy only                              (a green cone pair)
  uz       the top node at r = 6, 90 deg: uz held at 0.5                          (a blue cone pair and an orange dot)
  rot      the top node at r = 4.5, 180 deg: rx and ry                            (two rings)

Output: <out.inp> (the washer plus the boundary conditions) and <out.json>: {"constraints": [{"node", "dofs", "prescribed"}]}
sorted by node id, with the node ids written in the mesh, plus {"probes": {"ux": id, "uy": id, "uz": id, "rot": id}}.

usage: make_constrained_washer.py <washer.inp> <out.inp> <out.json>
"""
import json
import math
import sys


def read_nodes(path):
    nodes, mode = {}, False
    with open(path) as f:
        for line in f:
            if line.startswith("*"):
                mode = line.upper().startswith("*NODE")
                continue
            if mode:
                fields = [x.strip() for x in line.split(",") if x.strip()]
                if len(fields) >= 4:
                    nodes[int(fields[0])] = tuple(float(v) for v in fields[1:4])
    return nodes


def find(nodes, r, theta_deg, z):
    x, y = r * math.cos(math.radians(theta_deg)), r * math.sin(math.radians(theta_deg))
    best = min(nodes, key=lambda i: math.dist(nodes[i], (x, y, z)))
    assert math.dist(nodes[best], (x, y, z)) < 1e-6, (r, theta_deg, z)
    return best


def main(src, out_inp, out_json):
    nodes = read_nodes(src)
    rim = sorted(i for i, c in nodes.items() if abs(math.hypot(c[0], c[1]) - 6.0) < 1e-6 and abs(c[2]) < 1e-9)
    probes = {"ux": find(nodes, 3.0, 0, 2.0), "uy": find(nodes, 4.5, 270, 2.0), "uz": find(nodes, 6.0, 90, 2.0), "rot": find(nodes, 4.5, 180, 2.0)}
    text = open(src).read().rstrip("\n") + "\n*NSET, NSET=Rim\n"
    text += "".join(", ".join(str(i) for i in rim[k:k + 8]) + "\n" for k in range(0, len(rim), 8))
    text += "*BOUNDARY\nrim, 1, 3\n"
    text += f"{probes['ux']}, 1\n{probes['uy']}, 2, 2\n{probes['uz']}, 3, 3, 0.5\n{probes['rot']}, 4, 5\n"
    open(out_inp, "w").write(text)
    expected = {i: {"node": i, "dofs": ["ux", "uy", "uz"], "prescribed": False} for i in rim}
    expected[probes["ux"]] = {"node": probes["ux"], "dofs": ["ux"], "prescribed": False}
    expected[probes["uy"]] = {"node": probes["uy"], "dofs": ["uy"], "prescribed": False}
    expected[probes["uz"]] = {"node": probes["uz"], "dofs": ["uz"], "prescribed": True}
    expected[probes["rot"]] = {"node": probes["rot"], "dofs": ["rx", "ry"], "prescribed": False}
    json.dump({"constraints": [expected[i] for i in sorted(expected)], "probes": probes}, open(out_json, "w"), indent=1)
    print(f"{len(expected)} constrained nodes ({len(rim)} on the rim) -> {out_inp}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit("usage: make_constrained_washer.py <washer.inp> <out.inp> <out.json>")
    main(*sys.argv[1:])
