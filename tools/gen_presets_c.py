#!/usr/bin/env python3
"""Erzeugt core/gd_presets_data.c aus den GrooveDeck-Presets (Source/Presets.h und Source/ChannelPresets.h
des JUCE-Plugins). Die Texte werden in ein kompaktes Binaerformat uebersetzt (siehe core/gd_preset.c).

Aufruf:  python tools/gen_presets_c.py PFAD/ZU/GrooveDeck/Source  [--out core/gd_presets_data.c]
"""
import argparse
import re
import sys
from pathlib import Path

PARTS = ["Drum1", "Drum2", "Drum3", "Drum4", "Drum5", "Drum6A", "Drum6B", "Drum7A", "Drum7B",
         "Stretch1", "Stretch2", "Slice", "AudioIn", "Accent", "Keyboard1", "Keyboard2"]
CTL = ["startPoint", "reverse", "pitchGlide", "filterType", "cutoff", "resonance", "filterEgInt", "level", "pan",
       "egTime", "ampEg", "roll", "fxSend", "fxSelect", "modType", "modDepth", "modSpeed", "modDest", "modBpmSync",
       "motionSeq"]
EXTRA = ["wave", "lowBoost", "modNoise", "osc1", "osc2", "osc2p", "oscMod", "oscBal", "dist", "pmType", "pmDepth",
         "pmSpeed", "ampDecay", "fltModel", "fltTrack", "fltFM", "accDecay", "accSweep", "sweepSpd", "overdrv",
         "muffler", "modSine", "pmBar", "pmLoop", "modBar", "modLoop"]
ARP = ["on", "hold", "style", "grid", "oct", "gate", "shuffle", "accent", "chordOn", "form", "voicing", "len",
       "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7"]
PF_X0 = len(CTL)
PF_SRC = PF_X0 + len(EXTRA)
PF_TUBE = PF_SRC + 1
PF_A0 = PF_TUBE + 1
PF_COUNT = PF_A0 + len(ARP)
GLOBALS = ["tempo", "gate", "fxChain", "tubeMaster", "goodOn", "goodAmt", "goodStyle", "goodAuto", "masterVol",
           "fx1_type", "fx1_edit1", "fx1_edit2", "fx2_type", "fx2_edit1", "fx2_edit2", "fx3_type", "fx3_edit1", "fx3_edit2"]
PART_BASE = 64
CHAN_CATS = {"kKick": 0, "kSnare": 1, "kTom": 2, "kPerc": 3, "kClap": 4, "kHatC": 5, "kHatO": 6, "kCymbal": 7,
             "kBass": 14, "kLead": 15}


def enc_val(v):
    if abs(v - round(v)) < 1e-9 and -16384 <= round(v) <= 16383:
        return 0x8000 | (int(round(v)) + 16384)
    q = int(round(v * 10000))
    if not 0 <= q < 0x8000:
        raise ValueError("Wert ausserhalb des Bereichs: %r" % v)
    return q


def part_field(key):
    """'<Part>_<ctl>', 'x_<Part>_<extra>', 'src_<Part>', 'tube_<Part>', 'ap_<Part>_<arp>' -> (part, field)"""
    m = re.match(r"^x_(\w+?)_(\w+)$", key)
    if m and m.group(1) in PARTS and m.group(2) in EXTRA:
        return PARTS.index(m.group(1)), PF_X0 + EXTRA.index(m.group(2))
    m = re.match(r"^ap_(\w+?)_(\w+)$", key)
    if m and m.group(1) in PARTS and m.group(2) in ARP:
        return PARTS.index(m.group(1)), PF_A0 + ARP.index(m.group(2))
    m = re.match(r"^(src|tube)_(\w+)$", key)
    if m and m.group(2) in PARTS:
        return PARTS.index(m.group(2)), PF_SRC if m.group(1) == "src" else PF_TUBE
    m = re.match(r"^(\w+?)_(\w+)$", key)
    if m and m.group(1) in PARTS and m.group(2) in CTL:
        return PARTS.index(m.group(1)), CTL.index(m.group(2))
    return None


def encode_preset(text, stats):
    out = bytearray()
    for e in text.split(";"):
        e = e.strip()
        if "=" not in e:
            continue
        k, v = e.split("=", 1)
        k, v = k.strip(), v.strip()
        if k in ("s", "kn", "kl"):
            pname, body = v.split(":", 1)
            part = PARTS.index(pname)
            if k == "s":
                bits = [1 if c in "xX" else 0 for c in body[:64]]
                nb = (len(bits) + 7) // 8
                data = bytearray(nb)
                for i, b in enumerate(bits):
                    if b:
                        data[i >> 3] |= 1 << (i & 7)
                out += bytes([3, part, nb]) + data
            elif part >= 14:
                vals = [int(x) for x in body.split(",") if x.strip()][:64]
                if k == "kn":
                    vals = [max(0, min(127, x)) for x in vals]
                else:
                    vals = [max(1, min(16, x)) for x in vals]
                out += bytes([4 if k == "kn" else 5, part - 14, len(vals)]) + bytes(vals)
            continue
        if k == "pl":
            out += bytes([2, max(1, min(64, int(float(v))))])
            continue
        if k == "t":
            t = int(round(float(v) * 10))
            out += bytes([1, t & 255, t >> 8])
            continue
        if k == "masterVol":
            continue
        if k in GLOBALS:
            pid = GLOBALS.index(k)
        else:
            pf = part_field(k)
            if pf is None:
                stats.setdefault("unknown", set()).add(k)
                continue
            pid = PART_BASE + pf[0] * PF_COUNT + pf[1]
        val = enc_val(float(v))
        out += bytes([6, pid & 255, pid >> 8, val & 255, val >> 8])
    out.append(0)
    return bytes(out)


def encode_chan(text, stats):
    """Kanal-Preset: Felder relativ zum Part, Datensatz 7: u8 Feld, u16 Wert"""
    out = bytearray()
    for e in text.split(";"):
        if "=" not in e:
            continue
        k, v = (x.strip() for x in e.split("=", 1))
        if k == "src":
            f = PF_SRC
        elif k.startswith("x.") and k[2:] in EXTRA:
            f = PF_X0 + EXTRA.index(k[2:])
        elif k in CTL:
            f = CTL.index(k)
        else:
            stats.setdefault("unknown", set()).add(k)
            continue
        val = enc_val(float(v))
        out += bytes([7, f, val & 255, val >> 8])
    out.append(0)
    return bytes(out)


def c_str(s):
    s = s.encode("ascii", "replace").decode("ascii")
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def ascii_name(s):
    tr = {"ä": "ae", "ö": "oe", "ü": "ue", "Ä": "Ae", "Ö": "Oe", "Ü": "Ue", "ß": "ss", "é": "e", "è": "e", "–": "-", "’": "'"}
    return "".join(tr.get(c, c) for c in s)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, help="Ordner Source/ des GrooveDeck-Plugins")
    ap.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "core" / "gd_presets_data.c")
    a = ap.parse_args()
    ptxt = (a.source / "Presets.h").read_text(encoding="utf-8")
    ctxt = (a.source / "ChannelPresets.h").read_text(encoding="utf-8")
    genres = re.search(r"kGenres\[kNumGenres\]\s*=\s*\{(.*?)\};", ptxt, re.S).group(1)
    genres = re.findall(r'"([^"]*)"', genres)
    subs_block = re.search(r"kSubNames\[kNumGenres\]\[kSubsPerGenre\]\s*=\s*\{(.*?)\n\};", ptxt, re.S).group(1)
    subs = [re.findall(r'"([^"]*)"', row) for row in re.findall(r"\{([^{}]*)\}", subs_block)]
    entries = re.findall(r'\{\s*"([^"]*)",\s*(\d+),\s*"([^"]*)",\s*"(t=[^"]*)"\s*\}', ptxt)
    stats = {}
    blob = bytearray()
    pres = []
    for g, sub, name, data in entries:
        off = len(blob)
        blob += encode_preset(data, stats)
        pres.append((genres.index(g), int(sub), ascii_name(name), off))
    chans = []
    for arr, body in re.findall(r"inline const Preset (k\w+)\[\] = \{(.*?)\n\};", ctxt, re.S):
        if arr not in CHAN_CATS:
            continue
        for name, data in re.findall(r'\{\s*"([^"]*)",\s*"([^"]*)"\s*\}', body):
            if name.startswith("Sample"):
                continue  # Sample-Quellen gibt es auf dem FM-1 nicht
            off = len(blob)
            blob += encode_chan(data, stats)
            chans.append((CHAN_CATS[arr], ascii_name(name), off))
    if stats.get("unknown"):
        print("Unbekannte Schluessel (uebersprungen):", sorted(stats["unknown"]), file=sys.stderr)

    L = []
    L.append("/* AUTOMATISCH ERZEUGT von tools/gen_presets_c.py aus GrooveDeck Source/Presets.h und ChannelPresets.h.")
    L.append("   Nicht von Hand aendern. Format siehe core/gd_preset.c. */")
    L.append('#include "gd_preset.h"')
    L.append("#if !GD_PRESETS_MINI   /* FM-1 (FM1 Open): kleiner Satz in gd_presets_mini.c, Rest als Paket im Flash */")
    L.append("")
    L.append("const int gd_num_genres = %d;" % len(genres))
    L.append("const char *const gd_genre_names[] = { %s };" % ", ".join(c_str(ascii_name(g)) for g in genres))
    L.append("const char *const gd_sub_names[][5] = {")
    for row in subs:
        L.append("    { %s }," % ", ".join(c_str(ascii_name(s)) for s in row))
    L.append("};")
    L.append("const int gd_num_presets = %d;" % len(pres))
    L.append("const gd_preset_entry gd_presets[] = {")
    for g, sub, name, off in pres:
        L.append("    { %d, %d, %s, %d }," % (g, sub, c_str(name), off))
    L.append("};")
    L.append("const int gd_num_chan_presets = %d;" % len(chans))
    L.append("const gd_chan_entry gd_chan_presets[] = {")
    for cat, name, off in chans:
        L.append("    { %d, %s, %d }," % (cat, c_str(name), off))
    L.append("};")
    L.append("const unsigned gd_preset_blob_size = %d;" % len(blob))
    L.append("const uint8_t gd_preset_blob[] = {")
    for i in range(0, len(blob), 24):
        L.append("    " + ",".join(str(b) for b in blob[i:i + 24]) + ",")
    L.append("};")
    L.append("const unsigned gd_preset_blob_packed = 0;")
    L.append("#endif")
    a.out.write_text("\n".join(L) + "\n", encoding="ascii")
    print("%d Presets, %d Kanal-Presets, %d Byte Daten -> %s" % (len(pres), len(chans), len(blob), a.out))


if __name__ == "__main__":
    main()
