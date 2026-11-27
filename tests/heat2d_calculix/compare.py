#!/usr/bin/env python3
"""Manual check: Heat2D against CalculiX (ccx) on the same plate with a circular hole.

Not part of CTest: it needs the `ccx` executable on PATH and a built heat2d_run (see heat2d_run.cpp).

Usage:
    python3 compare.py HEAT2D_RUN_BINARY WORKDIR

The script writes plate.script, plate.inp and plate.map into WORKDIR (through make_case.py), runs ccx and the
heat2d_run binary, and prints the largest nodal difference on the z = 0 layer. The two discretisations use the same
element set and node positions, so the difference measures the solvers, not the geometry.
"""
import csv
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    heat2d_run, workdir = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    os.makedirs(workdir, exist_ok=True)
    subprocess.run([sys.executable, os.path.join(HERE, "make_case.py")], cwd=workdir, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["ccx", "-i", "plate"], cwd=workdir, check=True, stdout=subprocess.DEVNULL)
    with open(os.path.join(workdir, "heat2d.csv"), "w") as out:
        subprocess.run([heat2d_run, "plate.script"], cwd=workdir, check=True, stdout=out)

    ccx = {}
    inside = False
    with open(os.path.join(workdir, "plate.dat")) as f:
        for line in f:
            if "temperatures for set" in line:
                inside = True
                continue
            if inside:
                parts = line.split()
                if len(parts) == 2:
                    ccx[int(parts[0])] = float(parts[1])
                elif parts:
                    inside = False
    coord = {}
    with open(os.path.join(workdir, "plate.map")) as f:
        for line in f:
            i, x, y = line.split()
            coord[int(i)] = (round(float(x), 9), round(float(y), 9))
    h2d = {}
    with open(os.path.join(workdir, "heat2d.csv")) as f:
        for row in csv.DictReader(f):
            h2d[(round(float(row["x"]), 9), round(float(row["y"]), 9))] = float(row["temperature"])
    diffs = [abs(ccx[i] - h2d[p]) for i, p in coord.items()]
    print(f"nodes compared: {len(diffs)}")
    print(f"max |CalculiX - Heat2D| = {max(diffs):.3e} K (largest temperature {max(ccx.values()):.3f} K)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
