#!/usr/bin/env python3
"""GrooveDeck FM-1 - Firmware bauen (JieLi pi32v2-Toolchain + AC79-SDK + Treiber aus fm1-nes).

STATUS: ENTWURF, noch nicht ausgefuehrt. Aufbau nach dem Linkschritt von fm1-nes (firmware/usb-diag/build.py),
aber ohne USB/NES-Teile. Ergebnis: build/groovedeck.app.bin (fuer den fm1-emulator: "emulator groovedeck.app.bin").

Umgebungsvariablen (wie bei fm1-nes):
  FM1_NES_DIR        Klon von https://github.com/Keitark/fm1-nes            (Pflicht)
  FM1_SDK_DIR        AC79-SDK (Standard: $FM1_NES_DIR/.deps/ac79-sdk, per "python scripts/dependencies.py --fetch")
  FM1_TOOLCHAIN_DIR  Ordner mit clang(.exe), pi32v2-lto-wrapper(.exe), llvm-objcopy(.exe)  (Windows: C:/JL/pi32/bin)

Aufruf:  python target/build.py [--out build] [--y-offset 0|40] [--lean]
"""
import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CORE = ["gd_dsp.c", "gd_arp.c", "gd_seq.c", "gd_preset.c", "gd_presets_data.c", "gd_app.c", "gd_ui.c",
        "gd_ui_params.c", "gd_font.c", "gd_input.c"]
SPEED = {"gd_dsp.c", "gd_seq.c", "gd_arp.c"}   # mit -O2 statt der SDK-Voreinstellung


def make_list(text, name):
    m = re.search(r"^" + re.escape(name) + r"\s*:=\s*\\\n((?:.*\\\n)*)", text, re.M)
    if not m:
        raise SystemExit("SDK-Makefile: Liste fehlt: " + name)
    return m.group(1).replace("\\\n", " ").split()


def tool(tc, name):
    for cand in (tc / (name + ".exe"), tc / name):
        if cand.exists():
            return cand
    raise SystemExit("Werkzeug nicht gefunden: %s in %s" % (name, tc))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=ROOT / "build")
    ap.add_argument("--y-offset", type=int, default=0, help="Display-Zeilenversatz (0 oder 40)")
    ap.add_argument("--lean", action="store_true", help="Spar-Einstellungen: Hall mono, Analyse halbe Rate, 4 Stimmen")
    a = ap.parse_args()

    nes = Path(os.environ.get("FM1_NES_DIR", "")).resolve()
    if not (nes / "firmware" / "nes").is_dir():
        raise SystemExit("FM1_NES_DIR zeigt nicht auf einen fm1-nes-Klon")
    sdk = Path(os.environ.get("FM1_SDK_DIR", nes / ".deps" / "ac79-sdk")).resolve()
    tc = Path(os.environ.get("FM1_TOOLCHAIN_DIR", "C:/JL/pi32/bin")).resolve()
    make = sdk / "apps/demo/demo_hello/board/wl82/Makefile"
    if not make.exists():
        raise SystemExit("AC79-SDK nicht gefunden: " + str(make))
    clang, lto, objcopy = tool(tc, "clang"), tool(tc, "pi32v2-lto-wrapper"), tool(tc, "llvm-objcopy")
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    log = (out / "build.log").open("w", encoding="utf-8")

    def run(args):
        r = subprocess.run([str(x) for x in args], cwd=out, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log.write(" ".join(map(str, args)) + "\n" + r.stdout)
        log.flush()
        if r.returncode:
            print(r.stdout[-6000:])
            raise SystemExit("Fehler - Details in " + str(out / "build.log"))
        return r.stdout

    nb, ns = nes / "firmware/nes/boot", nes / "firmware/nes/src"
    text = make.read_text(encoding="utf-8")
    flags = make_list(text, "CFLAGS")
    defines = make_list(text, "DEFINES") + [
        "-DGD_TARGET_FM1=1", "-DGD_LCD_Y_OFFSET=%d" % a.y_offset,
        # Display-Treiber von fm1-nes im synchronen Werks-Ablauf
        "-DFM1_NES_PLAYER=1", "-DFM1_TARGET_PI32V2=1",
        "-DFM1_LCD_STOCK_FILL=1", "-DFM1_LCD_STOCK_DMA=1", "-DFM1_LCD_STOCK_SEQUENCE=1"]
    if a.lean:
        defines += ["-DGD_REVERB_STEREO=0", "-DGD_GOOD_FULLRATE=0", "-DGD_MAX_VOICES=4"]
    includes = ["-I" + str(x) for x in (ROOT / "core", HERE, nb, nes / "firmware/nes/include", sdk / "apps/common")]
    includes += ["-I" + str((make.parent / x[2:]).resolve()) for x in make_list(text, "INCLUDES")]

    sources = [(make.parent / x).resolve() for n in ("c_SRC_FILES", "S_SRC_FILES") for x in make_list(text, n)]
    sources = [s for s in sources if s.name not in ("app_main.c", "board.c", "cpp_run_init.c")]
    sources += [nb / n for n in ("board.c", "boot_compat.c", "boot_trace.c", "board_power.c", "display_test.c")]
    sources += [ns / n for n in ("fm1_wl82_keyscan.c", "fm1_stock_keys.c", "fm1_volume.c")]
    sources += [ROOT / "core" / n for n in CORE] + [HERE / "gd_fm1_main.c"]

    for src, name in ((sdk / "cpu/wl82/sdk_ld.c", "sdk.ld"), (sdk / "cpu/wl82/sdk_used_list.c", "sdk.used")):
        run([clang, *flags, *defines, *includes, "-D__LD__", "-E", "-P", src, "-o", out / name])
    used = out / "sdk.used"
    used.write_text(used.read_text() + "\nmemory_init\ngd_audio_task\ngd_ui_task\ngd_alink_isr\n")
    ld = (out / "sdk.ld").read_text()
    ld = ld.replace("*(.data)", "*(.data .data.*)", 1).replace("*(.bss)", "*(.bss .bss.*)", 1)
    (out / "sdk.ld").write_text(ld)

    objects = []
    for i, src in enumerate(sources):
        obj = out / ("%d-%s.o" % (i, src.name))
        f = flags
        if src.name in SPEED:
            f = [x for x in flags if x not in ("-O0", "-O1", "-O2", "-O3", "-Os", "-Oz")] + ["-O2"]
        run([clang, *f, *defines, *includes, "-c", src, "-o", obj])
        objects.append(obj)

    libs = [sdk / "include_lib/newlib/pi32v2-lib" / n for n in ("libm.a", "libc.a", "libcompiler_rt.a")]
    libs += [sdk / "cpu/wl82/liba" / n for n in ("cpu.a", "event.a", "system.a", "cfg_tool.a", "fs.a", "common_lib.a", "update.a")]
    elf = out / "groovedeck.elf"
    run([lto, "-o", elf, *objects, "--start-group", *libs, "--end-group", "-T" + str(out / "sdk.ld"),
         "-M=" + str(out / "groovedeck.map"), "--wrap=boot_info_init", "--wrap=memory_init", "--undefined=memory_init",
         "--plugin-opt=mcpu=r3", "--plugin-opt=-mattr=+fprev1", "--plugin-opt=-pi32v2-large-program=true",
         "--plugin-opt=-used-symbol-file=" + str(used)])
    parts = []
    for sec in (".text", ".data", ".dynamic_data", ".ram0_data", ".cache_ram_data"):
        f = out / (sec[1:] + ".bin")
        run([objcopy, "-O", "binary", "-j", sec, elf, f])
        parts.append(f.read_bytes())
    app = out / "groovedeck.app.bin"
    app.write_bytes(b"".join(parts))
    print("fertig: %s (%d Byte)" % (app, app.stat().st_size))
    print("Emulator:  emulator %s" % app)


if __name__ == "__main__":
    main()
