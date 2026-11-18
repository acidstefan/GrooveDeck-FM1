#!/usr/bin/env python3
"""Erzeugt aus core/gd_presets_data.c (alle Werkspresets) die Preset-Dateien fuer das FM-1 (FM1 Open):

  core/gd_presets_mini.c          kleiner Satz in der Firmware: je Richtung das erste Preset und alle
                                  Kanal-Presets, LZ-komprimiert (nur mit GD_PRESETS_MINI uebersetzt)
  fm1open/uploader/presets.bin    Preset-Paket fuer den Nutzerspeicher des FM-1 (alle Song-Presets)
  fm1open/uploader/index.html     Webseite zum Aufspielen des Pakets per USB-MIDI (Paket eingebettet)

Aufruf:  python tools/gen_presets_pack.py

Paket (Little Endian), liegt im Nutzerspeicher ab 8192 (davor der Benutzerstand):
  0  "GDP1"   4 Anzahl   8 Gesamtlaenge   12 FNV-1a ueber [16, Gesamtlaenge)   16 Meta-Offset   20 Daten-Offset
  Meta je Preset 32 Byte: u32 Offset in Daten, u16 Laenge, u8 Richtung, u8 Untergruppe, char Name[24]
LZ-Format siehe gd_lz_extract in core/gd_preset.c."""
import base64
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def parse(path):
    s = path.read_text(encoding="ascii")
    genres = re.findall(r'"((?:[^"\\]|\\.)*)"', s[s.index("gd_genre_names[]"):s.index("gd_sub_names")])
    subs_part = s[s.index("gd_sub_names"):s.index("const int gd_num_presets")]
    subs = [re.findall(r'"((?:[^"\\]|\\.)*)"', row) for row in re.findall(r"\{([^{}]*)\}", subs_part)]
    ent = r'\{\s*(\d+),\s*(?:(\d+),\s*)?"((?:[^"\\]|\\.)*)",\s*(\d+)\s*\}'
    pres = [(int(g), int(sb), n.replace('\\"', '"').replace("\\\\", "\\"), int(o))
            for g, sb, n, o in re.findall(ent, s[s.index("gd_presets[]"):s.index("const int gd_num_chan_presets")])]
    chans = [(int(c), n.replace('\\"', '"').replace("\\\\", "\\"), int(o))
             for c, _, n, o in re.findall(ent, s[s.index("gd_chan_presets[]"):s.index("gd_preset_blob_size")])]
    size = int(re.search(r"gd_preset_blob_size = (\d+);", s).group(1))
    i = s.index("gd_preset_blob[] = {")
    blob = bytes(int(x) for x in re.findall(r"\d+", s[i + 20:s.index("};", i)]))
    assert len(blob) == size
    return genres, subs, pres, chans, blob


def lz(d):
    """LZSS: Flagbyte (Bit 0 zuerst), Verweis 2 Byte ((Abstand-1)<<6 | (Laenge-3)), Fenster 1024, Laenge 3..66."""
    out = bytearray()
    i = 0
    heads = {}
    while i < len(d):
        fp = len(out)
        out.append(0)
        for bit in range(8):
            if i >= len(d):
                break
            best, bd = 0, 0
            for j in reversed(heads.get(d[i:i + 3], [])):
                if i - j > 1024:
                    break
                n = 0
                while n < 66 and i + n < len(d) and d[j + n] == d[i + n]:
                    n += 1
                if n > best:
                    best, bd = n, i - j
            if best >= 3:
                out[fp] |= 1 << bit
                v = ((bd - 1) << 6) | (best - 3)
                out += bytes([v >> 8, v & 255])
                step = best
            else:
                out.append(d[i])
                step = 1
            for k in range(i, i + step):
                heads.setdefault(d[k:k + 3], []).append(k)
            i += step
    return bytes(out)


def unlz(src, n):
    out = bytearray()
    i = 0
    while len(out) < n:
        f = src[i]; i += 1
        for bit in range(8):
            if len(out) >= n:
                break
            if f >> bit & 1:
                v = src[i] << 8 | src[i + 1]; i += 2
                d, l = (v >> 6) + 1, (v & 63) + 3
                for _ in range(l):
                    out.append(out[-d])
            else:
                out.append(src[i]); i += 1
    return bytes(out[:n])


def fnv(b):
    h = 0x811C9DC5
    for x in b:
        h = ((h ^ x) * 0x01000193) & 0xFFFFFFFF
    return h


def main():
    genres, subs, pres, chans, blob = parse(ROOT / "core" / "gd_presets_data.c")
    song_end = chans[0][2] if chans else len(blob)
    ends = [p[3] for p in pres[1:]] + [song_end]

    # ---- kleiner Satz: je Richtung das erste Preset (Untergruppe 0), dazu alle Kanal-Presets
    pick = []
    for g in range(len(genres)):
        cand = [k for k, p in enumerate(pres) if p[0] == g and p[1] == 0] or [k for k, p in enumerate(pres) if p[0] == g]
        if cand:
            pick.append(cand[0])
    raw = bytearray()
    mini = []
    for k in pick:
        mini.append((pres[k][0], pres[k][1], pres[k][2], len(raw)))
        raw += blob[pres[k][3]:ends[k]]
    base = len(raw)
    raw += blob[song_end:]
    mchans = [(c, n, o - song_end + base) for c, n, o in chans]
    packed = lz(bytes(raw))
    assert unlz(packed, len(raw)) == bytes(raw)

    L = ["/* AUTOMATISCH ERZEUGT von tools/gen_presets_pack.py aus gd_presets_data.c. Nicht von Hand aendern.",
         "   Kleiner Preset-Satz fuer das FM-1 (GD_PRESETS_MINI): je Richtung ein Song-Preset und alle Kanal-Presets,",
         "   LZ-komprimiert. Alle %d Song-Presets kommen als Paket in den Nutzerspeicher (fm1open/uploader). */" % len(pres),
         '#include "gd_preset.h"', "#if GD_PRESETS_MINI", "",
         "const int gd_num_genres = %d;" % len(genres),
         "const char *const gd_genre_names[] = { %s };" % ", ".join(c_str(g) for g in genres),
         "const char *const gd_sub_names[][5] = {"]
    L += ["    { %s }," % ", ".join(c_str(x) for x in row) for row in subs]
    L += ["};", "const int gd_num_presets = %d;" % len(mini), "const gd_preset_entry gd_presets[] = {"]
    L += ["    { %d, %d, %s, %d }," % (g, sb, c_str(n), o) for g, sb, n, o in mini]
    L += ["};", "const int gd_num_chan_presets = %d;" % len(mchans), "const gd_chan_entry gd_chan_presets[] = {"]
    L += ["    { %d, %s, %d }," % (c, c_str(n), o) for c, n, o in mchans]
    L += ["};", "const unsigned gd_preset_blob_size = %d;   /* entpackt */" % len(raw),
          "const unsigned gd_preset_blob_packed = %d;" % len(packed), "const uint8_t gd_preset_blob[] = {"]
    L += ["    " + ",".join(str(b) for b in packed[i:i + 24]) + "," for i in range(0, len(packed), 24)]
    L += ["};", "#endif"]
    (ROOT / "core" / "gd_presets_mini.c").write_text("\n".join(L) + "\n", encoding="ascii")

    # ---- Paket fuer den Nutzerspeicher
    meta = bytearray()
    data = bytearray()
    for k, (g, sb, n, o) in enumerate(pres):
        rec = blob[o:ends[k]]
        name = n.encode("ascii")[:23]
        meta += struct.pack("<IHBB24s", len(data), len(rec), g, sb, name)
        data += rec
    meta_off = 32
    blob_off = meta_off + len(meta)
    total = blob_off + len(data)
    body = struct.pack("<II8x", meta_off, blob_off) + meta + data
    pack = b"GDP1" + struct.pack("<III", len(pres), total, fnv(body)) + body
    assert len(pack) == total
    up = ROOT / "fm1open" / "uploader"
    up.mkdir(parents=True, exist_ok=True)
    (up / "presets.bin").write_bytes(pack)
    tpl = (up / "index.template.html").read_text(encoding="utf-8")
    html = tpl.replace("@@PACK_B64@@", base64.b64encode(pack).decode("ascii")).replace("@@COUNT@@", str(len(pres)))
    (up / "index.html").write_text(html, encoding="utf-8")
    print("Mini: %d Song- + %d Kanal-Presets, %d -> %d Byte; Paket: %d Presets, %d Byte"
          % (len(mini), len(mchans), len(raw), len(packed), len(pres), total))


if __name__ == "__main__":
    main()
