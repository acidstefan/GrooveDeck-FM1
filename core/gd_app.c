/* GrooveDeck FM-1 - Bedienlogik: Seiten, Regler, Tastatur, Step-Eingabe, Live-Aufnahme, Speichern. */
#include "gd_app.h"
#include "gd_ui_params.h"
#include "gd_math.h"
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------ Ereignisse */
static void post(gd_app *a, int type, int x, int y)
{
    const unsigned w = a->qw, next = (w + 1u) % GD_EVQ;
    if (next == a->qr) return; /* voll: Ereignis verwerfen */
    a->q[w].type = (uint8_t)type;
    a->q[w].a = (uint8_t)x;
    a->q[w].b = (int16_t)y;
    a->qw = next;
}
void gd_app_post_key(gd_app *a, int key, int down) { post(a, EV_KEY, key, down); }
void gd_app_post_button(gd_app *a, int btn, int down) { post(a, EV_BTN, btn, down); }
void gd_app_post_encoder(gd_app *a, int enc, int delta) { post(a, EV_ENC, enc, delta); }
void gd_app_post_master(gd_app *a, float v01) { post(a, EV_MASTER, 0, gd_roundi(gd_clampf(v01, 0.0f, 1.0f) * 1000.0f)); }

static void message(gd_app *a, const char *m)
{
    strncpy(a->msg, m, sizeof a->msg - 1);
    a->msg[sizeof a->msg - 1] = 0;
    a->msgMs = 1500;
}

void gd_app_tick(gd_app *a, int ms)
{
    if (a->msgMs > 0) { a->msgMs -= ms; if (a->msgMs <= 0) { a->msgMs = 0; ++a->uiStamp; } }
}

static void load_preset(gd_app *a, int idx)
{
    char m[40];
    const int playing = a->seq.playing;
    if (idx < 0) idx = gd_num_presets - 1;
    if (idx >= gd_num_presets) idx = 0;
    a->preset = idx;
    gd_preset_load(&a->seq, idx);
    a->seq.playing = playing;
    memset(a->chanSel, 0xff, sizeof a->chanSel);
    a->stepPage = 0;
    snprintf(m, sizeof m, "%03d %s", idx + 1, gd_presets[idx].name);
    message(a, m);
}

void gd_app_init(gd_app *a, float sampleRate)
{
    int i;
    memset(a, 0, sizeof *a);
    gd_engine_init(&a->eng, sampleRate);
    gd_seq_init(&a->seq, &a->eng);
    for (i = 0; i < GD_KEYS; ++i) { a->keyNote[i] = -1; a->keyPart[i] = -1; }
    a->lastNote[0] = 36;
    a->lastNote[1] = 60;
    a->part = 0;
    a->stepMode = 1;
    load_preset(a, 0);
    a->msgMs = 0;
}

int gd_app_load_user(gd_app *a, const uint8_t *data, int len)
{
    if (len <= 0 || len > (int)sizeof a->user) return -1;
    memcpy(a->user, data, (size_t)len);
    a->userLen = len;
    a->userValid = 1;
    return 0;
}

/* --------------------------------------------------------------- Parameter */
static float *pref(gd_app *a, const gd_pdesc *d)
{
    if (d->scope == SC_GLOBAL) return &a->eng.g[d->field];
    if (d->scope == SC_PART) return &a->eng.parts[a->part].p[d->field];
    return 0;
}

float gd_param_value(const gd_app *a, const gd_pdesc *d)
{
    switch (d->scope) {
        case SC_GLOBAL: return a->eng.g[d->field];
        case SC_PART: return a->eng.parts[a->part].p[d->field];
        case SC_PATLEN: return (float)a->seq.pat.patLen;
        case SC_CHAN: return (float)a->chanSel[a->part];
        case SC_MUTE: return (float)((a->seq.muteMask >> a->part) & 1u);
        default: return 0.0f;
    }
}

static void param_change(gd_app *a, const gd_pdesc *d, int delta)
{
    float v = gd_param_value(a, d);
    if (d->kind == PK_CONT) v = gd_clampf(v + 0.01f * (float)delta, d->lo, d->hi);
    else if (d->kind == PK_TEMPO) v = gd_clampf((float)gd_roundi(v) + (float)delta, d->lo, d->hi);
    else if (d->scope == SC_CHAN) v = (float)gd_clampi(gd_roundi(v) + delta, 0, gd_chan_preset_count(a->part) - 1);
    else v = (float)gd_clampi(gd_roundi(v) + delta, (int)d->lo, (int)d->hi);
    switch (d->scope) {
        case SC_GLOBAL:
        case SC_PART: {
            float *p = pref(a, d);
            *p = v;
            if (d->scope == SC_PART && d->field == PF_CHORD_FORM && gd_roundi(v) < GA_FORMS) {
                const int *sl = gd_arp_form_slots(gd_roundi(v));
                int i;
                for (i = 0; i < GA_SLOTS; ++i) a->eng.parts[a->part].p[PF_SLOT0 + i] = (float)sl[i];
            }
            gd_engine_update(&a->eng);
            break;
        }
        case SC_PATLEN:
            a->seq.pat.patLen = gd_roundi(v);
            if (a->stepPage * 16 >= a->seq.pat.patLen) a->stepPage = (a->seq.pat.patLen - 1) / 16;
            break;
        case SC_CHAN: {
            const int i = gd_roundi(v), idx = gd_chan_preset_nth(a->part, i);
            if (idx >= 0) {
                a->chanSel[a->part] = i;
                gd_chan_preset_apply(&a->seq, a->part, idx);
                message(a, gd_chan_presets[idx].name);
            }
            break;
        }
        case SC_MUTE:
            if (gd_roundi(v)) a->seq.muteMask |= (uint16_t)(1u << a->part);
            else a->seq.muteMask &= (uint16_t)~(1u << a->part);
            break;
        default: break;
    }
}

/* ------------------------------------------------------------- Tastatur */
static int is_ui_part(int p) { return p <= 8 || p == GD_ACCENT || p == GD_KBD1 || p == GD_KBD2; }

static void set_step(gd_app *a, int part, int step, int on, int note)
{
    gd_pattern *pt = &a->seq.pat;
    if (step < 0 || step >= GD_MAX_STEPS) return;
    if (on) pt->stepMask[part] |= (uint64_t)1 << step;
    else pt->stepMask[part] &= ~((uint64_t)1 << step);
    if (on && gd_is_kbd(part)) { pt->kbdNote[part - GD_KBD1][step] = (uint8_t)note; pt->kbdLen[part - GD_KBD1][step] = 1; }
}

static void key_event(gd_app *a, int key, int down)
{
    const int part = a->part;
    if (key < 0 || key >= GD_KEYS) return;
    if (a->stepMode && down && key < 16) {             /* STEP: Tasten 1-16 schalten Steps */
        const int step = a->stepPage * 16 + key;
        if (step < a->seq.pat.patLen) {
            const int on = !((a->seq.pat.stepMask[part] >> step) & 1u);
            set_step(a, part, step, on, gd_is_kbd(part) ? a->lastNote[part - GD_KBD1] : 60);
        }
        return;
    }
    if (a->stepMode && key < 16) return;
    if (down) {
        int p = part, note;
        if (gd_is_kbd(part)) {
            note = gd_clampi(GD_KEY_BASE_NOTE + key + 12 * a->octave, 0, 127);
            if (a->stepMode && key >= 16) note = gd_clampi(GD_KEY_BASE_NOTE + 12 + (key - 16) + 12 * a->octave, 0, 127);
            a->lastNote[part - GD_KBD1] = note;
        } else {
            /* Drum gewaehlt: unterste neun Tasten = Drum 1..7B (Fingerdrumming), sonst der gewaehlte Part */
            if (!a->stepMode && key < 9) p = key;
            note = 60;
        }
        if (p == GD_ACCENT) return;
        a->keyNote[key] = note;
        a->keyPart[key] = p;
        gd_seq_key(&a->seq, p, note, 100, 1);
        if (a->rec && a->seq.playing) {                  /* Live-Aufnahme: auf den naechsten Step quantisieren */
            const int pl = a->seq.pat.patLen;
            int step = gd_roundi(a->seq.beat * 4.0f) % pl;
            if (step < 0) step += pl;
            set_step(a, p, step, 1, note);
        }
    } else if (a->keyPart[key] >= 0) {
        gd_seq_key(&a->seq, a->keyPart[key], a->keyNote[key], 0, 0);
        a->keyPart[key] = -1;
    }
}

static void select_part(gd_app *a, int delta)
{
    static const int order[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, GD_ACCENT, GD_KBD1, GD_KBD2 };
    int i, cur = 0;
    for (i = 0; i < 12; ++i) if (order[i] == a->part) cur = i;
    cur = gd_clampi(cur + delta, 0, 11);
    a->part = order[cur];
    if (a->page == PG_ARP && !gd_is_kbd(a->part)) a->page = PG_HOME;
}

static void do_save(gd_app *a)
{
    const int n = gd_preset_save(&a->seq, a->user, (int)sizeof a->user);
    if (n < 0) { message(a, "Speicher voll"); return; }
    a->userLen = n;
    a->userValid = 1;
    if (a->save_hook && a->save_hook(a->user, n) != 0) message(a, "Flash-Fehler");
    else message(a, "Gespeichert");
}

static void button_event(gd_app *a, int b, int down)
{
    static const int pageOf[GB_COUNT] = { PG_FX, PG_SEL, PG_ENV, PG_LFO, PG_EDIT, PG_GLO, PG_HOME, -1, PG_ARP, -1, -1, -1, -1, -1 };
    if (b < 0 || b >= GB_COUNT) return;
    a->btnDown[b] = down;
    if (!down) return;
    if (pageOf[b] >= 0) {
        const int pg = pageOf[b];
        if (pg == PG_ARP && !gd_is_kbd(a->part)) { message(a, "ARP nur fuer BS/LD"); return; }
        if (a->page == pg) a->sub ^= 1;      /* erneut druecken: Seite B */
        else { a->page = pg; a->sub = 0; }
        return;
    }
    switch (b) {
        case GB_PLAY:
            gd_seq_set_playing(&a->seq, !a->seq.playing);
            break;
        case GB_REC:
            a->rec = !a->rec;
            message(a, a->rec ? "Aufnahme an" : "Aufnahme aus");
            break;
        case GB_SEQ:
            a->stepMode = !a->stepMode;
            message(a, a->stepMode ? "STEP: Tasten = Steps" : "PLAY: Tasten = Noten");
            break;
        case GB_SAVE:
            if (a->btnDown[GB_SEL] && a->userValid) {      /* SEL + SAVE: gesicherten Stand laden */
                gd_preset_load_data(&a->seq, a->user, a->userLen);
                message(a, "Benutzerstand geladen");
            } else do_save(a);
            break;
        case GB_OCTDN:
        case GB_OCTUP: {
            const int d = b == GB_OCTUP ? 1 : -1;
            if (a->stepMode) {
                const int pages = (a->seq.pat.patLen + 15) / 16;
                a->stepPage = gd_clampi(a->stepPage + d, 0, pages - 1);
            } else a->octave = gd_clampi(a->octave + d, -3, 3);
            if (a->btnDown[GB_OCTDN] && a->btnDown[GB_OCTUP]) { a->octave = 0; a->stepPage = 0; }
            break;
        }
        default: break;
    }
}

static void encoder_event(gd_app *a, int enc, int delta)
{
    if (enc == GE_SELECT) { select_part(a, delta); return; }
    if (enc == GE_PRESETS) { load_preset(a, a->preset + delta); return; }
    if (enc == GE_ALGO) { /* Tempo */
        a->eng.g[GP_TEMPO] = gd_clampf((float)gd_roundi(a->eng.g[GP_TEMPO]) + (float)delta, 20.0f, 300.0f);
        gd_engine_update(&a->eng);
        return;
    }
    if (enc >= GE_K1 && enc <= GE_K4) {
        const gd_pdesc *d = gd_page_param(a->page, a->sub, a->part, enc - GE_K1);
        if (d) param_change(a, d, delta);
    }
}

static void apply_events(gd_app *a)
{
    while (a->qr != a->qw) {
        const gd_evt e = a->q[a->qr];
        a->qr = (a->qr + 1u) % GD_EVQ;
        switch (e.type) {
            case EV_KEY: key_event(a, e.a, e.b); break;
            case EV_BTN: button_event(a, e.a, e.b); break;
            case EV_ENC: encoder_event(a, e.a, e.b); break;
            case EV_MASTER: a->eng.g[GP_MASTERVOL] = (float)e.b / 1000.0f; break;
            default: break;
        }
        ++a->uiStamp;
    }
    if (!is_ui_part(a->part)) a->part = 0;
}

void gd_app_audio(gd_app *a, float *l, float *r, int n)
{
    int i;
    const int step = a->seq.currentStep;
    apply_events(a);
    for (i = 0; i < n; ++i) l[i] = r[i] = 0.0f;
    gd_seq_render(&a->seq, l, r, n);
    if (a->seq.currentStep != step) ++a->uiStamp;
}

void gd_app_audio_voices(gd_app *a, gd_bus *bus, int n)
{
    /* Sequencer-Ereignisse fuer diesen Block, dann nur die Stimmen (Master/FX rechnet der zweite Kern) */
    gd_seq *s = &a->seq;
    gd_engine *e = &a->eng;
    const int step = s->currentStep;
    float bps;
    apply_events(a);
    if (n > GD_BLK) n = GD_BLK;
    bps = e->bpm / (60.0f * e->sr);
    gd_seq_tick(s, s->beat, s->beat + bps * (float)n);
    if (s->playing) { e->beatPos = s->beat; s->beat += bps * (float)n; } else s->beat = 0.0f;
    gd_seq_wrap(s);
    gd_engine_render_voices(e, bus, n);
    if (s->currentStep != step) ++a->uiStamp;
}
