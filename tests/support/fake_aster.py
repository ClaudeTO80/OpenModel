#!/usr/bin/env python3
"""
Stand-in for code_aster used by the tests: `fake_aster.py job.export` behaves like `run_aster job.export` as far as OpenModel can tell.

It reads the export file, checks that the command file and the mesh have the expected content, prints an Aster-like log and
writes the result tables the export file asks for (`F libr <file> R <unit>`), in the layout of IMPR_TABLE FORMAT='TABLEAU'
with separator ','. The values are made up (a linear deflection and a constant stress): the tests check the plumbing,
NOT the physics, and NOT that real code_aster accepts the generated command file.

Environment:
  FAKE_ASTER_FAIL    if set: print an error and exit with status 1 without writing tables
  FAKE_ASTER_NOTAB   if set: exit 0 without writing tables
  FAKE_ASTER_SLEEP   seconds to wait before finishing
"""
import os
import re
import sys
import time


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: fake_aster job.export", file=sys.stderr)
        return 2
    export = sys.argv[1]
    try:
        lines = open(export).read().splitlines()
    except OSError as e:
        print(f"<F> cannot open {export}: {e}")
        return 1
    files = {}
    for line in lines:
        parts = line.split()
        if len(parts) >= 5 and parts[0] == "F":
            files.setdefault(parts[1], []).append((parts[2], parts[3], int(parts[4])))
    for need in ("comm", "mail"):
        if need not in files or not os.path.isfile(files[need][0][0]):
            print(f"<F> no {need} file in the export file")
            return 1
    comm = open(files["comm"][0][0]).read()
    mail = open(files["mail"][0][0]).read()
    print(" Code_Aster fake version, for tests", flush=True)
    modal = "CALC_MODES" in comm
    for command in (["DEBUT(", "LIRE_MAILLAGE(", "AFFE_MODELE(", "DEFI_MATERIAU(", "FIN("] +
                    (["CALC_MODES(", "CALC_MATR_ELEM("] if modal else ["MECA_STATIQUE(", "CALC_CHAMP(", "DDL_IMPO"])):
        if command not in comm:
            print(f"<F> the command file has no {command}")
            return 1
    for block in ("COOR_3D", "FINSF", "FIN"):
        if block not in mail:
            print(f"<F> the mesh file has no {block}")
            return 1
    print(f" Analysis: {'modal' if modal else 'static'}", flush=True)
    if os.environ.get("FAKE_ASTER_FAIL"):
        print("<F> <DVP_1> fake failure: singular matrix", flush=True)
        return 1
    seconds = float(os.environ.get("FAKE_ASTER_SLEEP", "0"))
    if seconds > 0:
        time.sleep(seconds)
    if os.environ.get("FAKE_ASTER_NOTAB"):
        return 0

    # Nodes of the mesh: "N<id> x y z" between COOR_3D and FINSF.
    nodes = []
    section = False
    for line in mail.splitlines():
        t = line.split()
        if not t:
            continue
        if t[0] == "COOR_3D":
            section = True
        elif t[0] == "FINSF":
            section = False
        elif section:
            nodes.append((t[0], float(t[1]), float(t[2]), float(t[3])))
    nmodes = int(re.search(r"NMAX_FREQ=(\d+)", comm).group(1)) if modal else 0

    def write(path, header, rows):
        with open(path, "w") as f:
            f.write("#" + header + "\n")
            for r in rows:
                f.write(r + "\n")

    for path, _flag, unit in files.get("libr", []):
        name = os.path.basename(path)
        if name.endswith("_modes.tab"):
            write(path, "NUME_ORDRE,FREQ", [f"{k},{100.0 * k:.9E}" for k in range(1, nmodes + 1)])
        elif name.endswith("_modes_disp.tab"):
            rows = [f"{k},{n},{0.001 * k * x:.9E},{0.0:.9E},{-0.002 * k:.9E}" for k in range(1, nmodes + 1) for (n, x, y, z) in nodes]
            write(path, "NUME_ORDRE,NOEUD,DX,DY,DZ", rows)
        elif name.endswith("_disp.tab"):
            write(path, "NOEUD,DX,DY,DZ", [f"{n},{0.0:.9E},{-1e-3 * x:.9E},{0.0:.9E}" for (n, x, y, z) in nodes])
        elif name.endswith("_stress.tab"):
            write(path, "NOEUD,SIXX,SIYY,SIZZ,SIXY,SIXZ,SIYZ", [f"{n},{10.0 * x:.9E},0.0,0.0,{1.0:.9E},{2.0:.9E},{3.0:.9E}" for (n, x, y, z) in nodes])
        elif name.endswith("_strain.tab"):
            write(path, "NOEUD,EPXX,EPYY,EPZZ,EPXY,EPXZ,EPYZ", [f"{n},{1e-4 * x:.9E},0.0,0.0,{1e-5:.9E},{2e-5:.9E},{3e-5:.9E}" for (n, x, y, z) in nodes])
    print(" Job finished", flush=True)
    return 0


sys.exit(main())
