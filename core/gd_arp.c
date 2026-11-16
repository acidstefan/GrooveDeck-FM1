/* GrooveDeck FM-1 - Arpeggiator und Chord Variator, Portierung von GrooveDeck Source/Arp.h nach C99. */
#include "gd_arp.h"

enum { M_TABLE = 0, M_UP, M_DOWN, M_UPDOWN, M_DOWNUP, M_RANDOM, M_PLAYED, M_CHORD };
enum { T_TOP = 253, T_ALL = 254 };

static int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

const char *gd_arp_form_name(int i)
{
    static const char *n[GA_FORM_CHOICES] = {
        "Dur", "Moll", "Dom7", "Min7", "Sus4", "Sus2", "Maj7", "Dim", "Aug", "Power", "Add9", "Min9", "Dom9", "Maj9", "6", "Min6",
        "Dim7", "m7b5", "7sus4", "7#9", "13", "Quartal", "Cluster", "Oktaven", "Trance Stack", "Techno Stab", "Deep House", "Rave Stack",
        "Acid Fifth", "Lydian", "Phrygian", "Whole Tone",
        "User 1", "User 2", "User 3", "User 4", "User 5", "User 6", "User 7", "User 8" };
    return n[clampi(i, 0, GA_FORM_CHOICES - 1)];
}

const int *gd_arp_form_slots(int i)
{
    static const int s[GA_FORMS][GA_SLOTS] = {
        { 0, 4, 7, 12, 16, 12, 7, 4 }, { 0, 3, 7, 12, 15, 12, 7, 3 }, { 0, 4, 7, 10, 12, 10, 7, 4 }, { 0, 3, 7, 10, 12, 10, 7, 3 },
        { 0, 5, 7, 12, 17, 12, 7, 5 }, { 0, 2, 7, 12, 14, 12, 7, 2 }, { 0, 4, 7, 11, 12, 11, 7, 4 }, { 0, 3, 6, 12, 15, 12, 6, 3 },
        { 0, 4, 8, 12, 16, 12, 8, 4 }, { 0, 7, 12, 19, 24, 19, 12, 7 }, { 0, 4, 7, 14, 16, 14, 7, 4 }, { 0, 3, 7, 10, 14, 10, 7, 3 },
        { 0, 4, 7, 10, 14, 10, 7, 4 }, { 0, 4, 7, 11, 14, 11, 7, 4 }, { 0, 4, 7, 9, 12, 9, 7, 4 }, { 0, 3, 7, 9, 12, 9, 7, 3 },
        { 0, 3, 6, 9, 12, 9, 6, 3 }, { 0, 3, 6, 10, 12, 10, 6, 3 }, { 0, 5, 7, 10, 12, 10, 7, 5 }, { 0, 4, 7, 10, 15, 10, 7, 4 },
        { 0, 4, 10, 14, 21, 14, 10, 4 }, { 0, 5, 10, 15, 20, 15, 10, 5 }, { 0, 1, 2, 3, 4, 3, 2, 1 }, { 0, 12, 24, 12, 0, 12, 24, 12 },
        { 0, 12, 7, 16, 12, 19, 24, 19 }, { 0, 7, 10, 12, 15, 19, 15, 12 }, { 0, 3, 10, 14, 17, 14, 10, 3 }, { 0, 12, 19, 15, 24, 19, 15, 12 },
        { 0, 7, 0, 12, 7, 12, 19, 12 }, { 0, 4, 7, 11, 18, 11, 7, 4 }, { 0, 1, 7, 12, 13, 12, 7, 1 }, { 0, 2, 4, 6, 8, 6, 4, 2 } };
    return s[clampi(i, 0, GA_FORMS - 1)];
}

const char *gd_arp_voicing_name(int i)
{
    static const char *n[GA_VOICINGS] = { "Grundst.", "1. Umk.", "2. Umk.", "Drop 2", "Open" };
    return n[clampi(i, 0, GA_VOICINGS - 1)];
}
const char *gd_arp_grid_name(int i)
{
    static const char *n[6] = { "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return n[clampi(i, 0, 5)];
}
float gd_arp_grid_beats(int i)
{
    static const float g[6] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f };
    return g[clampi(i, 0, 5)];
}

typedef struct { const char *name; int mode; const char *dsl; } style_def;
static const style_def k_styles[GA_FACTORY] = {
    { "Up", M_UP, "" }, { "Down", M_DOWN, "" }, { "Up/Down", M_UPDOWN, "" }, { "Down/Up", M_DOWNUP, "" },
    { "Random", M_RANDOM, "" }, { "Played", M_PLAYED, "" }, { "Chord", M_CHORD, "" },
    { "Pulse", M_TABLE, "0! 0 0 0 0! 0 0 0 0! 0 0 0 0! 0 0 0" },
    { "Offbeat", M_TABLE, "r A r A! r A r A! r A r A! r A r A!" },
    { "Trance Gate", M_TABLE, "A! A r A A! r A r A! A r A A! r A r" },
    { "Rolling Bass", M_TABLE, "0! 0 0+ 0 0! 0 0+ 0 0! 0 0+ 0 0! 0 0+ 0" },
    { "Octave Pump", M_TABLE, "0! 0+ 0 0+ 0! 0+ 0 0+ 0! 0+ 0 0+ 0! 0+ 0 0+" },
    { "Rise", M_TABLE, "0! 1 2 3 0! 1 2 3 0! 1 2 3 0! 1 2 3" },
    { "Thumb Up", M_TABLE, "0! 1 0 2 0! 3 0 4 0! 1 0 2 0! 3 0 4" },
    { "Pinky Up", M_TABLE, "0! t 1 t 2! t 3 t 0! t 1 t 2! t 3 t" },
    { "Skip", M_TABLE, "0! r 2 r 1 r 3 r 0! r 2 r 1 r 3 r" } };

const char *gd_arp_style_name(int i)
{
    static const char *u[GA_USER_STYLES] = { "User 1", "User 2", "User 3", "User 4", "User 5", "User 6", "User 7", "User 8" };
    return i < GA_FACTORY ? k_styles[clampi(i, 0, GA_FACTORY - 1)].name : u[clampi(i - GA_FACTORY, 0, GA_USER_STYLES - 1)];
}

/* Code: Bits 0-7 Ton, 8-9 Oktave (1 = +1, 2 = -1), Bit 10 Akzent, Bit 11 Pause */
static int parse_token(const char *t, int len)
{
    int tone, oct = 0, acc = 0, i;
    if (t[0] == 'r') return 0x800;
    tone = t[0] == 't' ? T_TOP : t[0] == 'A' ? T_ALL : (t[0] - '0');
    for (i = 1; i < len; ++i) {
        if (t[i] == '+') oct = 1;
        else if (t[i] == '-') oct = 2;
        else if (t[i] == '!') acc = 1;
    }
    return (acc ? 0x400 : 0) | (oct << 8) | (tone & 0xFF);
}

int gd_arp_style_code(int style, int step)
{
    const char *p = k_styles[clampi(style, 0, GA_FACTORY - 1)].dsl;
    int n = 0;
    step = ((step % GA_STEPS) + GA_STEPS) % GA_STEPS;
    while (*p) {
        const char *s;
        while (*p == ' ') ++p;
        if (!*p) break;
        s = p;
        while (*p && *p != ' ') ++p;
        if (n == step) return parse_token(s, (int)(p - s));
        ++n;
    }
    return 0;
}

static void isort(int *a, int n)
{
    int i, j;
    for (i = 1; i < n; ++i) {
        const int v = a[i];
        for (j = i - 1; j >= 0 && a[j] > v; --j) a[j + 1] = a[j];
        a[j + 1] = v;
    }
}

static int voice_interval(const gd_arp_params *P, int i)
{
    const int len = clampi(P->slotLen, 1, GA_SLOTS);
    int pcs[GA_SLOTS], u = 0, j, k, pc, rank = 0, iv;
    for (j = 0; j < len; ++j) {
        const int c = ((P->slots[j] % 12) + 12) % 12;
        int have = 0;
        for (k = 0; k < u; ++k) if (pcs[k] == c) have = 1;
        if (!have) pcs[u++] = c;
    }
    isort(pcs, u);
    pc = ((P->slots[i] % 12) + 12) % 12;
    for (k = 0; k < u; ++k) if (pcs[k] == pc) rank = k;
    iv = P->slots[i];
    switch (P->voicing) {
        case 1: if (rank < 1) iv += 12; break;
        case 2: if (rank < 2) iv += 12; break;
        case 3: if (u >= 3 && rank == u - 2) iv -= 12; break;
        case 4: if (rank % 2 == 1) iv += 12; break;
        default: break;
    }
    return iv;
}

int gd_arp_chord_notes(const gd_arp_params *P, int key, int *out, int inSlotOrder)
{
    const int len = clampi(P->slotLen, 1, GA_SLOTS);
    int n = 0, i, k;
    for (i = 0; i < len; ++i) {
        const int note = key + voice_interval(P, i);
        int have = 0;
        if (note < 0 || note > 127) continue;
        for (k = 0; k < n; ++k) if (out[k] == note) have = 1;
        if (!have && n < GA_MAXPOOL) out[n++] = note;
    }
    if (!inSlotOrder) isort(out, n);
    return n;
}

void gd_arp_build_pool(const gd_arp_params *P, const int *held, int nHeld, gd_arp_pool *pool, int sorted)
{
    int base[GA_MAXPOOL], nb = 0, h, i, k, o, octs;
    for (h = 0; h < nHeld; ++h) {
        int tmp[GA_MAXPOOL], n = 1;
        if (P->chordOn) n = gd_arp_chord_notes(P, held[h], tmp, 1);
        else tmp[0] = held[h];
        for (i = 0; i < n; ++i) {
            int have = 0;
            for (k = 0; k < nb; ++k) if (base[k] == tmp[i]) have = 1;
            if (!have && nb < GA_MAXPOOL) base[nb++] = tmp[i];
        }
    }
    if (sorted) isort(base, nb);
    pool->n = 0;
    octs = clampi(P->octs, 1, 4);
    for (o = 0; o < octs; ++o)
        for (i = 0; i < nb && pool->n < GA_MAXPOOL; ++i) {
            const int note = base[i] + 12 * o;
            if (note <= 127) pool->note[pool->n++] = note;
        }
}

static int pos_mod(long a, int m) { const int r = (int)(a % m); return r < 0 ? r + m : r; }

gd_arp_out gd_arp_pick(long k, const gd_arp_pool *pool, const gd_arp_params *P, uint32_t *rng)
{
    gd_arp_out o;
    const int N = pool->n;
    const int style = clampi(P->style, 0, GA_STYLES - 1);
    const int mode = style < GA_FACTORY ? k_styles[style].mode : M_TABLE;
    int i;
    o.n = 0;
    o.acc = 0;
    if (N <= 0) return o;
    o.acc = pos_mod(k, 4) == 0;
    switch (mode) {
        case M_UP:
        case M_PLAYED: o.notes[o.n++] = pool->note[pos_mod(k, N)]; break;
        case M_DOWN: o.notes[o.n++] = pool->note[N - 1 - pos_mod(k, N)]; break;
        case M_UPDOWN: {
            const int period = N > 1 ? 2 * N - 2 : 1, p = pos_mod(k, period);
            o.notes[o.n++] = pool->note[p < N ? p : period - p];
            break;
        }
        case M_DOWNUP: {
            const int period = N > 1 ? 2 * N - 2 : 1, p = pos_mod(k, period);
            o.notes[o.n++] = pool->note[p < N ? N - 1 - p : p - (N - 1)];
            break;
        }
        case M_RANDOM:
            *rng = *rng * 1664525u + 1013904223u;
            o.notes[o.n++] = pool->note[(*rng >> 8) % (uint32_t)N];
            break;
        case M_CHORD:
            for (i = 0; i < N && o.n < GA_MAXCHORD; ++i) o.notes[o.n++] = pool->note[i];
            break;
        default: {
            const int code = style < GA_FACTORY ? gd_arp_style_code(style, pos_mod(k, GA_STEPS)) : P->user[pos_mod(k, GA_STEPS)];
            const int tone = code & 0xFF, ob = (code >> 8) & 3;
            const int oct = ob == 1 ? 1 : ob == 2 ? -1 : 0;
            o.acc = (code & 0x400) != 0;
            if (code & 0x800) { o.n = 0; return o; }
            if (tone == T_ALL)
                for (i = 0; i < N && o.n < GA_MAXCHORD; ++i) o.notes[o.n++] = pool->note[i];
            else {
                const int idx = tone == T_TOP ? N - 1 : tone % N;
                const int note = pool->note[idx] + 12 * oct;
                if (note >= 0 && note <= 127) o.notes[o.n++] = note;
            }
            break;
        }
    }
    return o;
}
