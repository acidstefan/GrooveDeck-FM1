#!/bin/sh
# Baut die Browser-Fassung: web/gd_web.wasm und web/groovedeck-fm1.html (eine Datei, WebAssembly eingebettet).
# Braucht clang mit WebAssembly-Ziel und wasm-ld (z. B. Ubuntu: apt install clang lld). Keine weitere Bibliothek.
set -e
cd "$(dirname "$0")"
CC=${CC:-clang}
$CC --target=wasm32 -O2 -ffreestanding -nostdlib -fno-builtin-printf \
    -Ilibc -I../core -I../host \
    '-DGD_PANEL_HELP="Maus: klicken/halten, Regler: Rad oder ziehen   Tastatur: siehe Hilfe unter dem Bedienfeld"' \
    -Wl,--no-entry -Wl,--export-dynamic -Wl,--initial-memory=33554432 -Wl,-z,stack-size=1048576 \
    -o gd_web.wasm gd_web.c libc/libc.c ../host/gd_panel_draw.c ../core/*.c
python3 - <<'PY'
import base64
w = base64.b64encode(open("gd_web.wasm", "rb").read()).decode()
t = open("index.template.html", encoding="utf-8").read()
open("groovedeck-fm1.html", "w", encoding="utf-8").write(t.replace("@@WASM_B64@@", w))
PY
ls -l gd_web.wasm groovedeck-fm1.html
