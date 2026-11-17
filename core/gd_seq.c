/* GrooveDeck FM-1 - Sequencer und Arpeggiator-Laufzeit (Portierung aus GrooveDeck PluginProcessor.cpp). */
#include "gd_seq.h"
#include "gd_math.h"
#include <string.h>
#include <math.h>

#define GD_CHUNK 16 /* Ereignisse werden auf 16 Samples genau ausgeloest (0,36 ms bei 44,1 kHz) */

static int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

void gd_pattern_clear(gd_pattern *p)
{
    int k, i;
    memset(p->stepMask, 0, sizeof p->stepMask);
    for (k = 0; k < 2; ++k)
        for (i = 0; i < GD_MAX_STEPS; ++i) { p->kbdNote[k][i] = 60; p->kbdLen[k][i] = 1; }
    p->patLen = 16;
}

void gd_seq_init(gd_seq *s, gd_engine *e)
{
    int k, u, i;
    memset(s, 0, sizeof *s);
    s->eng = e;
    s->seqOn = 1;
    s->currentStep = -1;
    gd_pattern_clear(&s->pat);
    for (k = 0; k < 2; ++k) {
        s->arp[k].rng = 0x1234567u + (uint32_t)k;
        s->arp[k].lastVel = 100;
        for (u = 0; u < GA_USER_STYLES; ++u)
            for (i = 0; i < GA_STEPS; ++i) s->arp[k].user[u][i] = (int16_t)gd_arp_style_code(12, i); /* Voreinstellung "Rise" */
    }
}

void gd_seq_read_arp(const gd_seq *s, int part, gd_arp_params *P)
{
    const float *q = s->eng->parts[part].p;
    const int kp = part - GD_KBD1;
    int i;
    P->arpOn = q[PF_ARP_ON] > 0.5f;
    P->hold = q[PF_ARP_HOLD] > 0.5f;
    P->style = clampi(gd_roundi(q[PF_ARP_STYLE]), 0, GA_STYLES - 1);
    P->grid = clampi(gd_roundi(q[PF_ARP_GRID]), 0, 5);
    P->octs = clampi(gd_roundi(q[PF_ARP_OCT]) + 1, 1, 4);
    P->gate = gd_clampf(q[PF_ARP_GATE], 0.05f, 1.0f);
    P->shuffle = gd_clampf(q[PF_ARP_SHUFFLE], 0.0f, 1.0f);
    P->accent = gd_clampf(q[PF_ARP_ACCENT], 0.0f, 1.0f);
    P->chordOn = q[PF_CHORD_ON] > 0.5f;
    P->voicing = clampi(gd_roundi(q[PF_CHORD_VOICING]), 0, GA_VOICINGS - 1);
    P->slotLen = clampi(gd_roundi(q[PF_CHORD_LEN]) + 1, 1, GA_SLOTS);
    for (i = 0; i < GA_SLOTS; ++i) P->slots[i] = clampi(gd_roundi(q[PF_SLOT0 + i]), -12, 24);
    for (i = 0; i < GA_STEPS; ++i)
        P->user[i] = P->style >= GA_FACTORY ? s->arp[kp].user[P->style - GA_FACTORY][i] : 0;
}

static int is_muted(const gd_seq *s, int part) { return (s->muteMask >> part) & 1u; }

static void emit(gd_seq *s, int part, int note, int vel, int on)
{
    if (on) gd_engine_note_on(s->eng, part, note, vel);
    else gd_engine_note_off(s->eng, part, note);
}

static void add_pending(gd_seq *s, int part, int note, float offBeat)
{
    if (s->nPending >= (int)(sizeof s->pending / sizeof s->pending[0])) return;
    s->pending[s->nPending].part = part;
    s->pending[s->nPending].note = note;
    s->pending[s->nPending].offBeat = offBeat;
    ++s->nPending;
}

static void release_route(gd_seq *s, gd_arp_rt *R, int part, int note)
{
    const int rt = R->route[note];
    int i;
    R->route[note] = 0;
    if (rt == 1 || rt == 2) emit(s, part, note, 0, 0);
    else if (rt == 3) {
        for (i = 0; i < R->cnN[note]; ++i) emit(s, part, R->cn[note][i], 0, 0);
        R->cnN[note] = 0;
    }
}

static void kbd_in(gd_seq *s, int part, int note, int vel, int on)
{
    gd_arp_rt *R;
    gd_arp_params A;
    int i, j;
    if (!gd_is_kbd(part)) return;
    note = clampi(note, 0, 127);
    vel = clampi(vel, 1, 127);
    R = &s->arp[part - GD_KBD1];
    gd_seq_read_arp(s, part, &A);
    if (on) {
        int fresh;
        if (is_muted(s, part)) return;
        if (R->route[note] != 0) release_route(s, R, part, note);
        fresh = !R->phys[note];
        if (fresh) { R->phys[note] = 1; ++R->down; }
        if (A.arpOn) {
            int have = 0;
            if (fresh && A.hold && R->down == 1) R->nHeld = 0;
            for (i = 0; i < R->nHeld; ++i) if (R->held[i] == note) have = 1;
            if (!have && R->nHeld < 16) R->held[R->nHeld++] = note;
            R->lastVel = vel;
            R->route[note] = 4;
        } else if (A.chordOn) {
            int tmp[GA_MAXPOOL];
            const int cnt = gd_arp_chord_notes(&A, note, tmp, 0);
            const int n = cnt < GA_MAXCHORD ? cnt : GA_MAXCHORD;
            R->cnN[note] = (uint8_t)n;
            for (i = 0; i < n; ++i) { R->cn[note][i] = (uint8_t)tmp[i]; emit(s, part, tmp[i], vel, 1); }
            R->route[note] = 3;
        } else {
            emit(s, part, note, vel, 1);
            R->route[note] = 1;
        }
    } else {
        if (R->phys[note]) { R->phys[note] = 0; R->down = R->down > 0 ? R->down - 1 : 0; }
        if (R->route[note] == 4) {
            R->route[note] = 0;
            if (!A.hold)
                for (i = 0; i < R->nHeld; ++i)
                    if (R->held[i] == note) { for (j = i; j < R->nHeld - 1; ++j) R->held[j] = R->held[j + 1]; --R->nHeld; break; }
        } else release_route(s, R, part, note);
    }
}

void gd_seq_key(gd_seq *s, int part, int note, int vel, int on)
{
    if (part < 0 || part >= GD_PARTS || part == GD_ACCENT) return;
    if (gd_is_kbd(part)) kbd_in(s, part, note, vel, on);
    else if (on) { if (!is_muted(s, part)) emit(s, part, note, vel, 1); }
    else emit(s, part, note, 0, 0);
}

static void fire_off(gd_seq *s, const gd_pending *p)
{
    if (gd_is_kbd(p->part)) kbd_in(s, p->part, p->note, 0, 0);
    else emit(s, p->part, p->note, 0, 0);
}

void gd_seq_panic(gd_seq *s)
{
    int k;
    s->nPending = 0;
    for (k = 0; k < 2; ++k) {
        gd_arp_rt *R = &s->arp[k];
        R->nHeld = 0; R->down = 0; R->nOffs = 0; R->wasOn = 0;
        memset(R->phys, 0, sizeof R->phys);
        memset(R->route, 0, sizeof R->route);
        memset(R->cnN, 0, sizeof R->cnN);
    }
    gd_engine_all_off(s->eng);
}

void gd_seq_set_playing(gd_seq *s, int on)
{
    s->playing = on ? 1 : 0;
}

static void process_arp(gd_seq *s, float b0, float b1)
{
    int kp, i, j;
    for (kp = 0; kp < 2; ++kp) {
        const int part = GD_KBD1 + kp;
        gd_arp_rt *R = &s->arp[kp];
        gd_arp_params A;
        gd_seq_read_arp(s, part, &A);
        if (!A.arpOn) {
            if (R->wasOn) {
                for (i = 0; i < R->nOffs; ++i) emit(s, part, R->offs[i].note, 0, 0);
                R->nOffs = 0;
                R->nHeld = 0;
                for (i = 0; i < 128; ++i) if (R->route[i] == 4) R->route[i] = 0;
                R->wasOn = 0;
            }
            continue;
        }
        R->wasOn = 1;
        if (!A.hold && R->down == 0) R->nHeld = 0;
        if (R->nHeld > 0 && !is_muted(s, part)) {
            gd_arp_pool pool;
            const float g = gd_arp_grid_beats(A.grid);
            long k, k0, k1;
            gd_arp_build_pool(&A, R->held, R->nHeld, &pool, A.style != 5);
            k0 = gd_floorl(b0 / g) - 1;
            k1 = gd_ceill(b1 / g) + 1;
            for (k = k0; k <= k1; ++k) {
                const float t = (float)k * g + ((k & 1) ? A.shuffle * g * 0.5f : 0.0f);
                gd_arp_out o;
                float base, v;
                int vel;
                if (t < b0 || t >= b1) continue;
                o = gd_arp_pick(k, &pool, &A, &R->rng);
                if (o.n == 0) continue;
                base = (float)R->lastVel;
                v = o.acc ? base + A.accent * (127.0f - base) : base * (1.0f - 0.35f * A.accent);
                vel = clampi(gd_roundi(v), 1, 127);
                for (i = 0; i < o.n; ++i) {
                    const int note = o.notes[i];
                    for (j = 0; j < R->nOffs; ++j)
                        if (R->offs[j].note == note) {
                            emit(s, part, note, 0, 0);
                            R->offs[j] = R->offs[--R->nOffs];
                            break;
                        }
                    emit(s, part, note, vel, 1);
                    if (R->nOffs < 64) { R->offs[R->nOffs].note = note; R->offs[R->nOffs].offBeat = t + A.gate * g; ++R->nOffs; }
                }
            }
        }
        for (i = 0; i < R->nOffs;) {
            if (R->offs[i].offBeat < b1) { emit(s, part, R->offs[i].note, 0, 0); R->offs[i] = R->offs[--R->nOffs]; }
            else ++i;
        }
    }
}

static void due_offs(gd_seq *s, float b1)
{
    int i;
    for (i = 0; i < s->nPending;) {
        if (s->pending[i].offBeat < b1) {
            const gd_pending p = s->pending[i];
            s->pending[i] = s->pending[--s->nPending];
            fire_off(s, &p);
        } else ++i;
    }
}

void gd_seq_tick(gd_seq *s, float b0, float b1)
{
    const gd_engine *e = s->eng;
    const float gate = gd_clampf(e->g[GP_GATE], 0.05f, 0.95f);
    int i;
    if (s->playing) {
        long st;
        if (!s->wasPlaying) s->nPending = 0;
        due_offs(s, b1);
        for (st = gd_ceill(b0 * 4.0f - 1e-6f); (float)st / 4.0f < b1; ++st) {
            const int pl = clampi(s->pat.patLen, 1, GD_MAX_STEPS);
            const int step = (int)(((st % pl) + pl) % pl);
            const float stepBeat = (float)st / 4.0f;
            int accent, vel, p;
            s->currentStep = step;
            if (!s->seqOn) continue;
            accent = (int)((s->pat.stepMask[GD_ACCENT] >> step) & 1u);
            vel = accent ? 127 : 100;
            for (p = 0; p < GD_PARTS; ++p) {
                int note = 60;
                float lenSteps = 1.0f;
                if (p == GD_ACCENT || is_muted(s, p) || !((s->pat.stepMask[p] >> step) & 1u)) continue;
                if (gd_is_kbd(p)) {
                    note = s->pat.kbdNote[p - GD_KBD1][step];
                    lenSteps = (float)clampi(s->pat.kbdLen[p - GD_KBD1][step], 1, 16);
                    kbd_in(s, p, note, vel, 1);
                } else emit(s, p, note, vel, 1);
                add_pending(s, p, note, stepBeat + ((lenSteps - 1.0f) + gate) * 0.25f);
            }
        }
        due_offs(s, b1);
    } else if (s->wasPlaying) {
        for (i = 0; i < s->nPending; ++i) fire_off(s, &s->pending[i]);
        s->nPending = 0;
        s->currentStep = -1;
    }
    s->wasPlaying = s->playing;
    if (s->playing) { process_arp(s, b0, b1); s->arpFree = b1; }
    else { const float a0 = s->arpFree; s->arpFree = a0 + (b1 - b0); process_arp(s, a0, s->arpFree); }
}

void gd_seq_wrap(gd_seq *s)
{
    /* Laufzeit begrenzen (float-Genauigkeit): um ganze Pattern- und 64-Viertel-Vielfache zurueckstellen */
    const float W = 64.0f * (float)clampi(s->pat.patLen, 1, GD_MAX_STEPS);
    int i, k;
    if (s->beat >= W) {
        s->beat -= W;
        for (i = 0; i < s->nPending; ++i) s->pending[i].offBeat -= W;
        if (s->playing) {
            for (k = 0; k < 2; ++k) for (i = 0; i < s->arp[k].nOffs; ++i) s->arp[k].offs[i].offBeat -= W;
            s->arpFree -= W;
        }
    }
    if (!s->playing && s->arpFree >= 1024.0f) {
        s->arpFree -= 1024.0f;
        for (k = 0; k < 2; ++k) for (i = 0; i < s->arp[k].nOffs; ++i) s->arp[k].offs[i].offBeat -= 1024.0f;
    }
}

void gd_seq_render(gd_seq *s, float *l, float *r, int n)
{
    gd_engine *e = s->eng;
    int done = 0;
    while (done < n) {
        const int c = (n - done) < GD_CHUNK ? (n - done) : GD_CHUNK;
        const float bps = e->bpm / (60.0f * e->sr);
        const float b0 = s->beat, b1 = b0 + bps * (float)c;
        gd_seq_tick(s, b0, b1);
        if (s->playing) { e->beatPos = b0; s->beat = b1; }
        else s->beat = 0.0f;
        gd_seq_wrap(s);
        gd_engine_render(e, l + done, r + done, c);
        done += c;
    }
}
