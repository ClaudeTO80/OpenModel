"""Writes the same plate with a circular hole as (a) a heat 2d script and (b) a CalculiX heat deck (one C3D8 layer thick)."""
import math, sys
LX = LY = 1.0; NX = NY = 20; T_PL = 0.1; R = 0.2; CX = CY = 0.5; QSRC = 1.0; TH = 50.0; TSIDE = 0.0
hx, hy = LX/NX, LY/NY
def in_hole(x, y): return (x-CX)**2 + (y-CY)**2 < R*R
active = {}
for ej in range(NY):
    for ei in range(NX):
        xc, yc = (ei+0.5)*hx, (ej+0.5)*hy
        active[(ei, ej)] = not in_hole(xc, yc)
def nid(i, j): return j*(NX+1)+i
used = set()
for (ei, ej), a in active.items():
    if a:
        for (i, j) in [(ei,ej),(ei+1,ej),(ei+1,ej+1),(ei,ej+1)]: used.add((i, j))
# node numbering: bottom layer z=0 uses 1..N, top layer z=T uses N+1..2N
order = sorted(used, key=lambda p: nid(*p))
num = {p: k+1 for k, p in enumerate(order)}
N = len(order)
top = {p: N+k+1 for k, p in enumerate(order)}
# boundary node sets (all z layers), following the Heat2D edge rules
side = {"LEFT": set(), "RIGHT": set(), "BOTTOM": set(), "TOP": set(), "HOLE": set()}
for (ei, ej), a in active.items():
    if not a: continue
    corners = {"b": [(ei,ej),(ei+1,ej)], "t": [(ei,ej+1),(ei+1,ej+1)], "l": [(ei,ej),(ei,ej+1)], "r": [(ei+1,ej),(ei+1,ej+1)]}
    nb = {"b": (ei, ej-1), "t": (ei, ej+1), "l": (ei-1, ej), "r": (ei+1, ej)}
    def add(name, edge):
        for p in edge: side[name].add(p)
    if ej == 0: add("BOTTOM", corners["b"])
    elif not active[nb["b"]]: add("HOLE", corners["b"])
    if ej+1 == NY: add("TOP", corners["t"])
    elif not active[nb["t"]]: add("HOLE", corners["t"])
    if ei == 0: add("LEFT", corners["l"])
    elif not active[nb["l"]]: add("HOLE", corners["l"])
    if ei+1 == NX: add("RIGHT", corners["r"])
    elif not active[nb["r"]]: add("HOLE", corners["r"])
# Heat2D: temperature sides fix their nodes; the plate sides take TSIDE, the hole takes TH.
# Corner nodes shared by a plate side and the hole would conflict; the geometry above has none.
with open("plate.script", "w") as f:
    f.write(f"analysis heat 2d\nplate lx={LX} ly={LY} nx={NX} ny={NY} thickness={T_PL}\nmaterial k=1 rhocp=1\n")
    f.write(f"hole circle cx={CX} cy={CY} r={R}\nheat q={QSRC}\n")
    for s in ("left", "right", "bottom", "top"):
        f.write(f"temperature side={s} T={TSIDE}\n")
    f.write(f"hole temperature T={TH}\nstep steady\n")
def fmt(ids):
    ids = sorted(ids); out = []
    for k in range(0, len(ids), 16): out.append(", ".join(str(i) for i in ids[k:k+16]))
    return "\n".join(out)
with open("plate.inp", "w") as f:
    f.write("*NODE\n")
    for p in order:
        x, y = p[0]*hx, p[1]*hy
        f.write(f"{num[p]}, {x:.12g}, {y:.12g}, 0.0\n")
    for p in order:
        x, y = p[0]*hx, p[1]*hy
        f.write(f"{top[p]}, {x:.12g}, {y:.12g}, {T_PL:.12g}\n")
    f.write("*ELEMENT, TYPE=C3D8, ELSET=SOLID\n")
    e = 0
    for (ei, ej), a in sorted(active.items()):
        if not a: continue
        e += 1
        q = [(ei,ej),(ei+1,ej),(ei+1,ej+1),(ei,ej+1)]
        f.write(f"{e}, " + ", ".join(str(num[p]) for p in q) + ", " + ", ".join(str(top[p]) for p in q) + "\n")
    f.write("*MATERIAL, NAME=M1\n*CONDUCTIVITY\n1.0\n*SOLID SECTION, ELSET=SOLID, MATERIAL=M1\n")
    f.write("*STEP\n*HEAT TRANSFER, STEADY STATE\n*DFLUX\nSOLID, BF, " + f"{QSRC}\n")
    for name, pts in side.items():
        if not pts: continue
        ids = [num[p] for p in pts] + [top[p] for p in pts]
        f.write(f"*NSET, NSET={name}\n{fmt(ids)}\n")
    f.write("*NSET, NSET=Nall\n" + fmt([num[p] for p in order] + [top[p] for p in order]) + "\n")
    f.write("*BOUNDARY\n")
    for name, val in (("LEFT", TSIDE), ("RIGHT", TSIDE), ("BOTTOM", TSIDE), ("TOP", TSIDE), ("HOLE", TH)):
        if side[name]: f.write(f"{name}, 11, 11, {val}\n")
    f.write("*NODE PRINT, NSET=Nall\nNT\n*END STEP\n")
# mapping for comparison: id -> (x, y) for bottom layer
with open("plate.map", "w") as f:
    for p in order: f.write(f"{num[p]} {p[0]*hx:.12g} {p[1]*hy:.12g}\n")
print("nodes per layer", N, "elements", e, "boundary sets", {k: len(v) for k, v in side.items()})
