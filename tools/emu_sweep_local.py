#!/usr/bin/env python3
"""Prueft eine Firmware auf Befehlsformen, die der fm1-emulator ablehnt (nutzt dessen decode_sweep).
Aufruf: python3 tools/emu_sweep_local.py EMULATOR_QUELLEN DISASSEMBLY.txt FIRMWARE-emu.bin [AUSGABE.md]
  DISASSEMBLY.txt: <toolchain>/common/bin/objdump -d build/groovedeck.elf
"""
import sys
from pathlib import Path
EMU, DIS, BIN = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
OUT = Path(sys.argv[4]) if len(sys.argv) > 4 else Path("luecken.md")
sys.path.insert(0, str(EMU / "tools"))
import decode_sweep as ds
ds.CACHE = Path("/tmp/gd-sweep-cache")
app = BIN.read_bytes()
code = {a: v for a, v in ds.parse(DIS.read_text()).items() if ds.BASE <= a < ds.BASE + len(app)}
live = ds.reachable(code, app)
lines = ds.report("GrooveDeck FM-1", BIN, app, code, live, ds.sweep(app, code.keys()))
OUT.write_text("\n".join(lines))
print("\n".join(lines[:12]))
