/* GrooveDeck FM-1 - Preset-Decoder.
   Binaerformat je Preset (Datensaetze, beendet mit 0):
     1 lo hi          Tempo * 10
     2 n              Pattern-Laenge (1..64)
     3 part nb b...   Step-Maske (Bit i = Step i), nb Bytes
     4 kp n b...      Keyboard-Noten (kp 0/1)
     5 kp n b...      Keyboard-Laengen in 16teln
     6 idlo idhi vlo vhi   Parameter: id < 64 global (GP_*), sonst 64 + part*PF_COUNT + Feld
     7 feld vlo vhi   (nur Kanal-Presets) Feld des gewaehlten Parts
   Wert: Bit 15 gesetzt -> Ganzzahl (v & 0x7FFF) - 16384, sonst v / 10000. */
#include "gd_preset.h"
#include "gd_math.h"

static float dec_val(unsigned v)
{
    if (v & 0x8000u) return (float)((int)(v & 0x7FFFu) - 16384);
    return (float)v / 10000.0f;
}

static void set_field(gd_engine *e, int part, int f, float v)
{
    int i;
    if (part < 0 || part >= GD_PARTS || f < 0 || f >= PF_COUNT) return;
    e->parts[part].p[f] = v;
    if (f == PF_CHORD_FORM) { /* Akkordform laedt die acht Notenabstaende (wie im Plugin-Editor) */
        const int form = gd_roundi(v);
        if (form >= 0 && form < GA_FORMS) {
            const int *sl = gd_arp_form_slots(form);
            for (i = 0; i < GA_SLOTS; ++i) e->parts[part].p[PF_SLOT0 + i] = (float)sl[i];
        }
    }
}

static int decode(gd_seq *s, const uint8_t *d, const uint8_t *end, int chanPart)
{
    gd_engine *e = s->eng;
    int i;
    while (d < end && *d) {
        switch (*d) {
            case 1: e->g[GP_TEMPO] = (float)(d[1] | (d[2] << 8)) / 10.0f; d += 3; break;
            case 2: s->pat.patLen = gd_clampi(d[1], 1, GD_MAX_STEPS); d += 2; break;
            case 3: {
                const int part = d[1], nb = d[2];
                uint64_t m = 0;
                for (i = 0; i < nb && i < 8; ++i) m |= (uint64_t)d[3 + i] << (8 * i);
                if (part < GD_PARTS) s->pat.stepMask[part] = m;
                d += 3 + nb;
                break;
            }
            case 4:
            case 5: {
                const int kp = d[1] & 1, n = d[2];
                for (i = 0; i < n && i < GD_MAX_STEPS; ++i) {
                    if (*d == 4) s->pat.kbdNote[kp][i] = d[3 + i];
                    else s->pat.kbdLen[kp][i] = d[3 + i];
                }
                d += 3 + n;
                break;
            }
            case 6: {
                const int id = d[1] | (d[2] << 8);
                const float v = dec_val((unsigned)(d[3] | (d[4] << 8)));
                if (id < GP_COUNT) { if (id != GP_MASTERVOL) e->g[id] = v; }
                else if (id >= 64) set_field(e, (id - 64) / PF_COUNT, (id - 64) % PF_COUNT, v);
                d += 5;
                break;
            }
            case 7:
                set_field(e, chanPart, d[1], dec_val((unsigned)(d[2] | (d[3] << 8))));
                d += 4;
                break;
            default: return -1; /* defekte Daten */
        }
    }
    return 0;
}

int gd_preset_load(gd_seq *s, int index)
{
    int r;
    if (index < 0 || index >= gd_num_presets) return -1;
    gd_seq_panic(s);
    gd_engine_reset_patch(s->eng);
    gd_pattern_clear(&s->pat);
    r = decode(s, gd_preset_blob + gd_presets[index].offset, gd_preset_blob + gd_preset_blob_size, -1);
    gd_engine_update(s->eng);
    return r;
}

int gd_chan_preset_cat(int part)
{
    if (part == 8) return 7;   /* Drum7B: Becken */
    return part;
}

int gd_chan_preset_count(int part)
{
    const int cat = gd_chan_preset_cat(part);
    int i, n = 0;
    for (i = 0; i < gd_num_chan_presets; ++i) if (gd_chan_presets[i].part == cat) ++n;
    return n;
}

int gd_chan_preset_nth(int part, int n)
{
    const int cat = gd_chan_preset_cat(part);
    int i;
    for (i = 0; i < gd_num_chan_presets; ++i)
        if (gd_chan_presets[i].part == cat && n-- == 0) return i;
    return -1;
}

int gd_chan_preset_apply(gd_seq *s, int part, int index)
{
    /* Karten 1-5 auf Standard: Regler ohne Level/Pan/FX/Roll/Motion, alle Extras ausser Low Boost, Quelle */
    static const int ctl[] = { PF_START, PF_REVERSE, PF_PITCH, PF_FTYPE, PF_CUTOFF, PF_RESO, PF_EGINT, PF_EGTIME,
                               PF_AMPEG, PF_MODTYPE, PF_MODDEPTH, PF_MODSPEED, PF_MODDEST, PF_MODSYNC };
    gd_engine *e = s->eng;
    unsigned i;
    int r;
    if (index < 0 || index >= gd_num_chan_presets || part < 0 || part >= GD_PARTS || part == GD_ACCENT) return -1;
    for (i = 0; i < sizeof ctl / sizeof ctl[0]; ++i) e->parts[part].p[ctl[i]] = gd_part_default(part, ctl[i]);
    for (i = PF_X0; i < PF_SRC; ++i) if ((int)i != PF_LOWBOOST) e->parts[part].p[i] = gd_part_default(part, (int)i);
    e->parts[part].p[PF_SRC] = gd_part_default(part, PF_SRC);
    r = decode(s, gd_preset_blob + gd_chan_presets[index].offset, gd_preset_blob + gd_preset_blob_size, part);
    gd_engine_update(e);
    return r;
}

/* ------------------------------------------------------------ Sichern */
static int put(uint8_t *buf, int cap, int *n, const uint8_t *src, int len)
{
    int i;
    if (*n + len > cap) return -1;
    for (i = 0; i < len; ++i) buf[*n + i] = src[i];
    *n += len;
    return 0;
}

static unsigned enc_val(float v)
{
    const int iv = gd_roundi(v);
    if ((float)iv == v && iv >= -16384 && iv <= 16383) return 0x8000u | (unsigned)(iv + 16384);
    return (unsigned)gd_clampi(gd_roundi(v * 10000.0f), 0, 0x7FFF);
}

static int put_param(uint8_t *buf, int cap, int *n, int id, float v)
{
    const unsigned e = enc_val(v);
    uint8_t r[5];
    r[0] = 6; r[1] = (uint8_t)(id & 255); r[2] = (uint8_t)(id >> 8); r[3] = (uint8_t)(e & 255); r[4] = (uint8_t)(e >> 8);
    return put(buf, cap, n, r, 5);
}

int gd_preset_save(const gd_seq *s, uint8_t *buf, int cap)
{
    const gd_engine *e = s->eng;
    int n = 0, p, f, i, k;
    uint8_t r[3 + GD_MAX_STEPS];
    const int t = gd_roundi(e->g[GP_TEMPO] * 10.0f);
    r[0] = 1; r[1] = (uint8_t)(t & 255); r[2] = (uint8_t)(t >> 8);
    if (put(buf, cap, &n, r, 3)) return -1;
    r[0] = 2; r[1] = (uint8_t)s->pat.patLen;
    if (put(buf, cap, &n, r, 2)) return -1;
    for (p = 0; p < GD_PARTS; ++p) {
        if (!s->pat.stepMask[p]) continue;
        r[0] = 3; r[1] = (uint8_t)p; r[2] = 8;
        for (i = 0; i < 8; ++i) r[3 + i] = (uint8_t)(s->pat.stepMask[p] >> (8 * i));
        if (put(buf, cap, &n, r, 11)) return -1;
    }
    for (k = 0; k < 2; ++k) {
        const int pl = s->pat.patLen;
        r[0] = 4; r[1] = (uint8_t)k; r[2] = (uint8_t)pl;
        for (i = 0; i < pl; ++i) r[3 + i] = s->pat.kbdNote[k][i];
        if (put(buf, cap, &n, r, 3 + pl)) return -1;
        r[0] = 5;
        for (i = 0; i < pl; ++i) r[3 + i] = s->pat.kbdLen[k][i];
        if (put(buf, cap, &n, r, 3 + pl)) return -1;
    }
    for (f = 1; f < GP_COUNT; ++f)
        if (f != GP_MASTERVOL && e->g[f] != gd_global_default(f) && put_param(buf, cap, &n, f, e->g[f])) return -1;
    for (p = 0; p < GD_PARTS; ++p)
        for (f = 0; f < PF_COUNT; ++f) {
            if (f == PF_CHORD_FORM) continue;    /* Form zuletzt: setzt sonst die Slots zurueck */
            if (e->parts[p].p[f] != gd_part_default(p, f) && put_param(buf, cap, &n, 64 + p * PF_COUNT + f, e->parts[p].p[f])) return -1;
        }
    for (p = GD_KBD1; p <= GD_KBD2; ++p) { /* Form vor den Slots wiederherstellen: erst Form, dann Slots erneut */
        const int f = PF_CHORD_FORM;
        if (e->parts[p].p[f] != gd_part_default(p, f)) {
            if (put_param(buf, cap, &n, 64 + p * PF_COUNT + f, e->parts[p].p[f])) return -1;
            for (i = 0; i < 8; ++i)
                if (put_param(buf, cap, &n, 64 + p * PF_COUNT + PF_SLOT0 + i, e->parts[p].p[PF_SLOT0 + i])) return -1;
        }
    }
    if (n + 1 > cap) return -1;
    buf[n++] = 0;
    return n;
}

int gd_preset_load_data(gd_seq *s, const uint8_t *data, int len)
{
    int r;
    if (!data || len <= 0) return -1;
    gd_seq_panic(s);
    gd_engine_reset_patch(s->eng);
    gd_pattern_clear(&s->pat);
    r = decode(s, data, data + len, -1);
    gd_engine_update(s->eng);
    return r;
}
