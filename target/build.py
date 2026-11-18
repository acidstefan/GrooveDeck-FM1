#!/usr/bin/env python3
"""GrooveDeck FM-1 - Firmware bauen (JieLi pi32v2-Toolchain + AC79-SDK + Treiber aus fm1-nes).

STATUS: ENTWURF, noch nicht ausgefuehrt. Aufbau nach dem Linkschritt von fm1-nes (firmware/usb-diag/build.py),
aber ohne USB/NES-Teile. Ergebnis: build/groovedeck.app.bin (fuer den fm1-emulator: "emulator groovedeck.app.bin").

Umgebungsvariablen (wie bei fm1-nes):
  FM1_NES_DIR        Klon von https://github.com/Keitark/fm1-nes            (Pflicht)
  FM1_SDK_DIR        AC79-SDK (Standard: $FM1_NES_DIR/.deps/ac79-sdk, per "python scripts/dependencies.py --fetch")
  FM1_TOOLCHAIN_DIR  Ordner mit clang(.exe) und lto-wrapper  (Linux: .../jieli-linux-toolchains-*/pi32v2/bin,
                     Windows: C:/JL/pi32/bin)

Aufruf:  python target/build.py [--out build] [--y-offset 0|40] [--lean] [--emu-light] [--single-core] [--flash-save]
"""
import argparse
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
CORE = ["gd_dsp.c", "gd_arp.c", "gd_seq.c", "gd_preset.c", "gd_presets_data.c", "gd_app.c", "gd_ui.c",
        "gd_ui_params.c", "gd_font.c", "gd_input.c", "gd_leds.c"]
SPEED = {"gd_dsp.c", "gd_seq.c", "gd_arp.c"}   # mit -O2 statt der SDK-Voreinstellung


def make_list(text, name):
    m = re.search(r"^" + re.escape(name) + r"\s*:=\s*\\\n((?:.*\\\n)*)", text, re.M)
    if not m:
        raise SystemExit("SDK-Makefile: Liste fehlt: " + name)
    return m.group(1).replace("\\\n", " ").split()


def tool(tc, names):
    """Sucht ein Werkzeug unter mehreren Namen in der Toolchain (auch im Nachbarordner common/bin
    der Linux-Toolchain, z. B. .../pi32v2/bin und .../common/bin)."""
    dirs = [tc, tc.parent.parent / "common" / "bin", tc.parent / "common" / "bin"]
    for d in dirs:
        for n in names:
            for cand in (d / (n + ".exe"), d / n):
                if cand.exists():
                    return cand
    raise SystemExit("Werkzeug nicht gefunden: %s (gesucht in %s)" % (" / ".join(names), ", ".join(map(str, dirs))))


FCMP_SLOT = {0x0: 0x80, 0x1: 0x88, 0x2: 0x90, 0x3: 0x98, 0x8: 0xC0, 0x9: 0xC8,
             0xA: 0xD0, 0xB: 0xD8, 0xC: 0xE0, 0xD: 0xE8}
TRAMP_MARK = (0x52544447).to_bytes(4, "little")
BASE = 0x02000120


def long_goto(here, target):
    """GOTO mit 22-Bit-Sprungweite (EAC0): Ziel = here + 4 + 2 * Weite."""
    delta = (target - (here + 4)) // 2
    if not -(1 << 21) <= delta < (1 << 21):
        raise SystemExit("Emulator-Fassung: Sprung zu weit")
    delta &= (1 << 22) - 1
    return struct.pack("<HH", 0xEAC0 | (delta >> 16), delta & 0xFFFF)


def fcmp_trampolines(d, disasm, funcs, tramp):
    """Gleitkomma-Vergleich mit weitem Sprung: FF4c rL/rR/0x80 + 16-Bit-Weite (6 Byte). Der fm1-emulator kennt
    davon nur die Ganzzahlform; die kurze Gleitkommaform (E800..EE80 | rR, rL/0x0800/Weite) kennt er, mit
    derselben Bedingung (wie auf dem FM-1 gemessen). Jede Stelle wird 'goto T' + nop; in T steht:
    IFF Bedingung rL, rR -> T+8 / goto Stelle+6 / T+8: goto Ziel. Die Objdump-Ausgabe des Herstellers markiert
    genau diese Befehle als <unknown instruction> und liefert damit sichere Befehlsgrenzen."""
    sites = []
    for ln in disasm.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+(4[0-9a-f]) ff\s+<unknown instruction>", ln)
        if not m:
            continue
        a = int(m.group(1), 16)
        o = a - BASE
        if o < 0 or o + 6 > len(d) or not any(s <= a and a + 6 <= s + n for s, n in funcs):
            continue
        op, x, disp = struct.unpack_from("<HHh", d, o)
        if (op & 0xFFF0) != 0xFF40 or (op & 15) not in FCMP_SLOT or (x & 0xFF) != 0x80:
            continue
        sites.append((a, op, x, disp))
    if not sites:
        return 0
    if tramp is None:
        raise SystemExit("Emulator-Fassung: gd_emu_tramp fehlt in der Symbolliste")
    o = tramp - BASE
    o += d[o:o + 64].find(TRAMP_MARK) if TRAMP_MARK in d[o:o + 64] else 0
    if d[o:o + 4] != TRAMP_MARK:
        raise SystemExit("Emulator-Fassung: Platz fuer Sprungumwege (gd_emu_tramp) nicht gefunden")
    room = 0
    while d[o + room:o + room + 4] == TRAMP_MARK:
        room += 4
    if 12 * len(sites) > room:
        raise SystemExit("Emulator-Fassung: zu viele Gleitkomma-Spruenge (%d) fuer gd_emu_tramp" % len(sites))
    t = BASE + o
    for a, op, x, disp in sites:
        rl, rr = x >> 12, (x >> 8) & 15
        to = t - BASE
        d[to:to + 4] = struct.pack("<HH", 0xE000 | (FCMP_SLOT[op & 15] << 4) | rr, (rl << 12) | 0x0800 | 2)
        d[to + 4:to + 8] = long_goto(t + 4, a + 6)
        d[to + 8:to + 12] = long_goto(t + 8, a + 6 + disp * 2)
        d[a - BASE:a - BASE + 6] = long_goto(a, t) + b"\x00\x00"
        t += 12
    return len(sites)


def emulator_patch(img, symbols=None, disasm=None):
    """Fassung fuer den fm1-emulator: Der kennt 'clz' nicht als Haelfte eines Parallelbefehls
    (F180 xxxx + 16-Bit-Befehl). Solche Paare werden in 'Schwanz, dann clz' (E180, seriell) umgestellt.
    Nur wenn das gleichwertig ist: Der Schwanz ist ein Lade-/Speicherbefehl der Familie 0x6000 und
    schreibt nicht das Quellregister von clz. Auf dem Geraet wird die unveraenderte Datei verwendet."""
    d = bytearray(img)
    n = 0
    for i in range(0, len(d) - 6, 2):
        if d[i] != 0x80 or d[i + 1] != 0xF1:
            continue
        x = d[i + 2] | (d[i + 3] << 8)
        tail = d[i + 4] | (d[i + 5] << 8)
        if x & 0xFF or (tail & 0xE008) != 0x6000:
            continue
        src = (x >> 8) & 15
        writes = None if tail & 0x80 else tail & 7
        if writes == src:
            continue
        d[i:i + 6] = bytes([d[i + 4], d[i + 5], 0x80, 0xE1, d[i + 2], d[i + 3]])
        n += 1
    print("Emulator-Fassung: %d Parallelbefehl(e) umgestellt" % n)
    # Init-Tabelle: sdfile_init (sucht ein Flash-Dateisystem, das es bei roher .bin nicht gibt) -> gd_initcall_noop
    if symbols:
        addr, funcs = {}, []
        for line in symbols.splitlines():
            parts = line.split()
            if len(parts) >= 3 and len(parts[0]) == 8:
                addr[parts[-1]] = int(parts[0], 16)
            if len(parts) == 4 and parts[2] in "tT" and parts[3] != "gd_emu_tramp":
                funcs.append((int(parts[0], 16), int(parts[1], 16)))
        flag = addr.get("gd_emu_flag")
        fo = flag - 0x02000120 if flag is not None else -1
        if 0 <= fo <= len(d) - 4 and d[fo:fo + 4] == (0x47444530).to_bytes(4, "little"):
            d[fo:fo + 4] = (0x47444531).to_bytes(4, "little")
            print("Emulator-Fassung: direkter Audiotreiber eingeschaltet")
        else:
            print("Hinweis: gd_emu_flag liegt nicht im Flash-Bild")
        slot, noop = addr.get("__initcall_sdfile_init"), addr.get("gd_initcall_noop")
        if slot is not None and noop is not None:
            off = slot - 0x02000120
            if 0 <= off <= len(d) - 4:
                d[off:off + 4] = noop.to_bytes(4, "little")
                print("Emulator-Fassung: sdfile_init in der Init-Tabelle ersetzt")
        else:
            print("Hinweis: __initcall_sdfile_init/gd_initcall_noop nicht in der Symbolliste")
        # Register-Schnappschuss von display_test.c (liest u. a. GPIO-PC-Eingaenge, die der Emulator nicht
        # nachbildet; Ergebnis wird ohnehin verworfen) -> sofort zurueck (rts)
        snap = addr.get("snapshot")
        if snap is not None and 0 <= snap - BASE < len(d):
            d[snap - BASE:snap - BASE + 2] = b"\x80\x00"
            print("Emulator-Fassung: Register-Schnappschuss der Display-Initialisierung abgeschaltet")
        if disasm:
            n = fcmp_trampolines(d, disasm, funcs, addr.get("gd_emu_tramp"))
            print("Emulator-Fassung: %d Gleitkomma-Vergleich(e) mit weitem Sprung umgeleitet" % n)
            # Speicher-XOR mit Konstante (EF40..EF5F, z. B. aus "x ^= 1") kennt der Emulator nicht: nur melden
            xs = re.findall(r"^\s*([0-9a-f]+):\s+[4-5][0-9a-f] ef [0-9a-f]{2} [0-9a-f]{2}\s+\[.*\^=", disasm, re.M)
            if xs:
                print("Warnung: %d Speicher-XOR-Befehl(e), im Emulator ein Abbruch, falls erreicht: %s"
                      % (len(xs), " ".join("0x" + x for x in xs[:12])))
        else:
            print("Hinweis: kein objdump - Gleitkomma-Vergleiche mit weitem Sprung bleiben (Emulator bricht dort ab)")
    return bytes(d)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=ROOT / "build")
    ap.add_argument("--y-offset", type=int, default=0, help="Display-Zeilenversatz (0 oder 40)")
    ap.add_argument("--keyscan-pio", action="store_true",
                    help="Tastenmatrix wie fm1-nes-Standard per SPI2_BUF statt DMA (laeuft nicht im Emulator)")
    ap.add_argument("--lean", action="store_true", help="Spar-Einstellungen: Hall mono, Analyse halbe Rate, 4 Stimmen")
    ap.add_argument("--flash-save", action="store_true",
                    help="SAVE dauerhaft ins Flash (letzte 64 KB); noch ungetestet")
    ap.add_argument("--single-core", action="store_true",
                    help="Klangberechnung in einem Task (ohne Aufteilung auf zwei Kerne)")
    ap.add_argument("--emu-light", action="store_true",
                    help="nur fuer langsame PCs im Emulator: wie --lean, ein Kern, Engine mit halber Abtastrate (dumpfer)")
    a = ap.parse_args()

    nes = Path(os.environ.get("FM1_NES_DIR", "")).resolve()
    if not (nes / "firmware" / "nes").is_dir():
        raise SystemExit("FM1_NES_DIR zeigt nicht auf einen fm1-nes-Klon")
    sdk = Path(os.environ.get("FM1_SDK_DIR", nes / ".deps" / "ac79-sdk")).resolve()
    tc = Path(os.environ.get("FM1_TOOLCHAIN_DIR", "C:/JL/pi32/bin")).resolve()
    make = sdk / "apps/demo/demo_hello/board/wl82/Makefile"
    if not make.exists():
        raise SystemExit("AC79-SDK nicht gefunden: " + str(make))
    clang = tool(tc, ["clang"])
    lto = tool(tc, ["pi32v2-lto-wrapper", "lto-wrapper"])
    objcopy = tool(tc, ["llvm-objcopy", "pi32v2-objcopy", "objcopy", "llvm-objcopy-4.0"])
    print("Werkzeuge:", clang, lto, objcopy, sep="\n  ")
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
    if not a.keyscan_pio:
        # Tastenmatrix per SPI2-DMA wie die Original-Firmware (fm1-nes-Option); diese Betriebsart bildet der
        # fm1-emulator nach, die Einzelbyte-Uebertragung ueber SPI2_BUF nicht.
        defines += ["-DFM1_KEYSCAN_DMA2=1"]
    if a.lean or a.emu_light:
        defines += ["-DGD_REVERB_STEREO=0", "-DGD_GOOD_FULLRATE=0", "-DGD_MAX_VOICES=4"]
    if a.single_core or a.emu_light:
        # im Emulator rechnet ein PC-Kern beide FM-1-Kerne: die Aufteilung kostet dort nur zusaetzlich
        defines += ["-DGD_DUAL_CORE=0"]
    if a.flash_save:
        defines += ["-DGD_FLASH_SAVE=1"]
    if a.emu_light:
        defines += ["-DGD_ENGINE_DIV=2"]
        print("Hinweis: --emu-light rechnet mit 22,05 kHz - nur zum Testen im Emulator, nicht fuers Geraet")
    includes = ["-I" + str(x) for x in (ROOT / "core", HERE, nb, nes / "firmware/nes/include", sdk / "apps/common")]
    includes += ["-I" + str((make.parent / x[2:]).resolve()) for x in make_list(text, "INCLUDES")]

    sources = [(make.parent / x).resolve() for n in ("c_SRC_FILES", "S_SRC_FILES") for x in make_list(text, n)]
    sources = [s for s in sources if s.name not in ("app_main.c", "board.c", "cpp_run_init.c")]
    sources += [nb / n for n in ("board.c", "boot_trace.c", "board_power.c", "display_test.c")]
    sources += [HERE / "gd_boot_compat.c"]   # statt fm1-nes boot_compat.c: auch Start als rohe .bin im Emulator
    sources += [ns / n for n in ("fm1_wl82_keyscan.c", "fm1_stock_keys.c", "fm1_volume.c")]
    sources += [ROOT / "core" / n for n in CORE] + [HERE / "gd_fm1_main.c"]

    for src, name in ((sdk / "cpu/wl82/sdk_ld.c", "sdk.ld"), (sdk / "cpu/wl82/sdk_used_list.c", "sdk.used")):
        run([clang, *flags, *defines, *includes, "-D__LD__", "-E", "-P", src, "-o", out / name])
    used = out / "sdk.used"
    used.write_text(used.read_text() + "\nmemory_init\n__wrap_boot_info_init\ngd_initcall_noop\ngd_emu_flag\ngd_alnk_direct_isr\ngd_audio_task\ngd_master_task\ngd_ui_task\ngd_alink_isr\nfm1_display_snapshot\ngd_emu_tramp\ngd_flash_diag\n")
    ld = (out / "sdk.ld").read_text()
    ld = ld.replace("*(.data)", "*(.data .data.*)", 1).replace("*(.bss)", "*(.bss .bss.*)", 1)
    # wie fm1-nes: keine automatische Reparatur der Flash-Konfiguration beim Start
    for sec in (".syscfg.2.ops", ".syscfg.1.ops"):
        ld = ld.replace("*(" + sec + ")", "/* no persistent cfg repair */", 1)
    ld += "\nSECTIONS { /DISCARD/ : { *(.syscfg.2.ops) *(.syscfg.1.ops) } }\n"
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
    for nm, args in (("symbols.txt", ["-n", "-S"]),):
        nmtool = [t for t in (tc / "llvm-nm", tc / "nm", tc / "llvm-nm.exe") if t.exists()]
        if nmtool:
            (out / nm).write_text(run([nmtool[0], *args, elf]))
    syms = (out / "symbols.txt").read_text() if (out / "symbols.txt").exists() else None
    disasm = None
    try:
        objdump = tool(tc, ["llvm-objdump", "objdump", "pi32v2-objdump"])
        r = subprocess.run([str(objdump), "-d", str(elf)], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        disasm = r.stdout if r.returncode == 0 else None
        if disasm:
            (out / "disasm.txt").write_text(disasm)
    except SystemExit as e:
        print("Hinweis:", e)
    emu = out / "groovedeck-emu.bin"
    emu.write_bytes(emulator_patch(app.read_bytes(), syms, disasm))
    print("fertig: %s (%d Byte)" % (app, app.stat().st_size))
    print("Emulator:  emulator %s" % emu)


if __name__ == "__main__":
    main()
