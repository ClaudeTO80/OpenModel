#!/usr/bin/env python3
"""
Stand-in for CalculiX used by the tests: `fake_ccx.py -i job` behaves like `ccx -i job` as far as OpenModel can tell.

It reads job.inp (so a wrongly written input is noticed), prints a CalculiX-like log, and "solves" by copying
pre-made result files of the beam fixture next to the job: beam.frd/beam.dat for a static step, beam_modal.frd/.dat
when the input contains *FREQUENCY.  It computes nothing; the tests using it check the plumbing, not the physics.

Environment:
  FAKE_CCX_FIXTURES  directory holding the beam fixture files (required)
  FAKE_CCX_SLEEP     seconds to wait before finishing (to test progress and cancel)
  FAKE_CCX_FAIL      if set: print an error and exit with status 1 without writing results
  FAKE_CCX_NOFRD     if set: exit 0 without writing results
"""
import os
import shutil
import signal
import sys
import time


def main() -> int:
    if len(sys.argv) != 3 or sys.argv[1] != "-i":
        print("usage: fake_ccx -i jobname", file=sys.stderr)
        return 2
    job = sys.argv[2]
    try:
        with open(job + ".inp") as f:
            text = f.read().upper()
    except OSError as e:
        print(f" *ERROR: could not open input file {job}.inp: {e}")
        return 1
    print(" CalculiX Version fake, Copyright(C) 1998-2024 Guido Dhondt", flush=True)
    for keyword in ("*NODE", "*ELEMENT", "*MATERIAL", "*BOUNDARY", "*STEP"):
        if keyword not in text:
            print(f" *ERROR in fake ccx: no {keyword} in the input", flush=True)
            return 1
    frequency = "*FREQUENCY" in text
    print(f" Job {job}: {'frequency' if frequency else 'static'} analysis", flush=True)
    if os.environ.get("FAKE_CCX_FAIL"):
        print(" *ERROR in e_c3d: nonpositive jacobian determinant in element 1", flush=True)
        return 1
    seconds = float(os.environ.get("FAKE_CCX_SLEEP", "0"))
    if seconds > 0:
        if os.environ.get("FAKE_CCX_IGNORE_TERM"):
            signal.signal(signal.SIGTERM, signal.SIG_IGN)
        end = time.time() + seconds
        step = 0
        while time.time() < end:
            step += 1
            print(f" increment {step}", flush=True)
            time.sleep(0.1)
    if os.environ.get("FAKE_CCX_NOFRD"):
        return 0
    fixtures = os.environ["FAKE_CCX_FIXTURES"]
    stem = "beam_modal" if frequency else "beam"
    shutil.copy(os.path.join(fixtures, stem + ".frd"), job + ".frd")
    shutil.copy(os.path.join(fixtures, stem + ".dat"), job + ".dat")
    print(" Job finished", flush=True)
    return 0


sys.exit(main())
