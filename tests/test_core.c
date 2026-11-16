/* GrooveDeck FM-1 - Offline-Tests der hardwareunabhaengigen Teile (ohne Original-Plugin). */
#include "gd_app.h"
#include "gd_input.h"
#include "gd_math.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FEHLER: %s (%s:%d)\n", msg, __FILE__, __LINE__); ++fails; } } while (0)

static gd_app app, app2;
static gd_input in;
static uint8_t rows[GD_SCAN_ROWS];

static void rows_idle(void) { memset(rows, 0xFF, sizeof rows); }
static void cell(uint8_t c, int closedc) { if (closedc) rows[c >> 4] &= (uint8_t)~(1u << (c & 15)); else rows[c >> 4] |= (uint8_t)(1u << (c & 15)); }
static void scan2(void) { gd_input_scan(&in, rows, &app); gd_input_scan(&in, rows, &app); }
static int drain(gd_evt *out, int max) { int n = 0; while (app.qr != app.qw && n < max) { out[n++] = app.q[app.qr]; app.qr = (app.qr + 1u) % GD_EVQ; } return n; }

static void test_math(void)
{
    float x, me = 0, ml = 0;
    for (x = -30.0f; x < 30.0f; x += 0.00731f) { const double r = exp2(x); const double e = fabs(gd_exp2(x) - r) / r; if (e > me) me = (float)e; }
    for (x = 1e-5f; x < 1e5f; x *= 1.0011f) { const double e = fabs(gd_log2(x) - log2(x)); if (e > ml) ml = (float)e; }
    CHECK(me < 5e-7f, "gd_exp2 zu ungenau");
    CHECK(ml < 3e-6f, "gd_log2 zu ungenau");
    CHECK(gd_exp2(0.0f) == 1.0f, "gd_exp2(0) != 1");
}

static void test_input(void)
{
    gd_evt ev[32];
    int n, i, sum;
    gd_input_init(&in);
    rows_idle();
    scan2();
    CHECK(drain(ev, 32) == 0, "Ruhezustand erzeugt Ereignisse");
    cell(0x44, 1); /* Slot 14 = erste Taste */
    gd_input_scan(&in, rows, &app);
    CHECK(drain(ev, 32) == 0, "Entprellung fehlt (Ereignis nach einer Abtastung)");
    gd_input_scan(&in, rows, &app);
    n = drain(ev, 32);
    CHECK(n == 1 && ev[0].type == EV_KEY && ev[0].a == 0 && ev[0].b == 1, "Taste 1 gedrueckt");
    cell(0x44, 0); scan2(); n = drain(ev, 32);
    CHECK(n == 1 && ev[0].type == EV_KEY && ev[0].b == 0, "Taste 1 losgelassen");
    cell(0x02, 1); scan2(); n = drain(ev, 32);
    CHECK(n == 1 && ev[0].type == EV_BTN && ev[0].a == GB_PLAY, "PLAY-Button");
    cell(0x02, 0); scan2(); drain(ev, 32);
    /* Drehgeber KNOB 1 drei Rastungen im Uhrzeigersinn: 00 01 11 10 00 */
    for (i = 0; i < 3; ++i) {
        cell(0xa1, 1); gd_input_scan(&in, rows, &app);
        cell(0x91, 1); gd_input_scan(&in, rows, &app);
        cell(0xa1, 0); gd_input_scan(&in, rows, &app);
        cell(0x91, 0); gd_input_scan(&in, rows, &app);
    }
    n = drain(ev, 32);
    sum = 0;
    for (i = 0; i < n; ++i) if (ev[i].type == EV_ENC && ev[i].a == GE_K1) sum += ev[i].b;
    CHECK(sum == 3, "Drehgeber rechts: +3 erwartet");
    for (i = 0; i < 2; ++i) {
        cell(0x91, 1); gd_input_scan(&in, rows, &app);
        cell(0xa1, 1); gd_input_scan(&in, rows, &app);
        cell(0x91, 0); gd_input_scan(&in, rows, &app);
        cell(0xa1, 0); gd_input_scan(&in, rows, &app);
    }
    n = drain(ev, 32);
    sum = 0;
    for (i = 0; i < n; ++i) if (ev[i].type == EV_ENC && ev[i].a == GE_K1) sum += ev[i].b;
    CHECK(sum == -2, "Drehgeber links: -2 erwartet");
}

static void test_save_load(void)
{
    uint8_t buf[4096];
    int n, p, f, k, i, same = 1;
    float l[512], r[512];
    gd_app_init(&app2, 44100.0f);
    gd_preset_load(&app2.seq, 123);
    app2.eng.parts[GD_KBD2].p[PF_CUTOFF] = 0.3333f;
    app2.eng.g[GP_TEMPO] = 97.5f;
    app2.seq.pat.stepMask[3] = 0x8000000000000001ull;
    app2.seq.pat.patLen = 64;
    n = gd_preset_save(&app2.seq, buf, sizeof buf);
    CHECK(n > 0, "Sichern fehlgeschlagen");
    gd_app_init(&app, 44100.0f);
    CHECK(gd_preset_load_data(&app.seq, buf, n) == 0, "Laden fehlgeschlagen");
    for (p = 0; p < GD_PARTS; ++p)
        for (f = 0; f < PF_COUNT; ++f)
            if (fabsf(app.eng.parts[p].p[f] - app2.eng.parts[p].p[f]) > 1e-4f) { same = 0; printf("  Part %d Feld %d: %f / %f\n", p, f, app.eng.parts[p].p[f], app2.eng.parts[p].p[f]); }
    for (f = 0; f < GP_COUNT; ++f) if (f != GP_MASTERVOL && fabsf(app.eng.g[f] - app2.eng.g[f]) > 1e-4f) { same = 0; printf("  global %d\n", f); }
    for (p = 0; p < GD_PARTS; ++p) if (app.seq.pat.stepMask[p] != app2.seq.pat.stepMask[p]) same = 0;
    for (k = 0; k < 2; ++k) for (i = 0; i < 64; ++i)
        if (app.seq.pat.kbdNote[k][i] != app2.seq.pat.kbdNote[k][i] || app.seq.pat.kbdLen[k][i] != app2.seq.pat.kbdLen[k][i]) same = 0;
    CHECK(same && app.seq.pat.patLen == 64, "Gesicherter Stand weicht nach dem Laden ab");
    printf("  Benutzerstand: %d Byte\n", n);
    gd_app_audio(&app, l, r, 512);
}

static void test_seq_arp(void)
{
    /* Arp Up, 1/16, 1 Oktave, Akkord Dur auf einer Taste: 16 Noten pro Takt */
    gd_engine e;
    gd_seq s;
    float l[64], r[64];
    int i, notes = 0;
    unsigned seen = 0;
    gd_engine_init(&e, 44100.0f);
    gd_seq_init(&s, &e);
    e.parts[GD_KBD2].p[PF_ARP_ON] = 1;
    e.parts[GD_KBD2].p[PF_ARP_STYLE] = 0;
    e.parts[GD_KBD2].p[PF_ARP_GRID] = 3;
    e.parts[GD_KBD2].p[PF_ARP_OCT] = 0;
    e.parts[GD_KBD2].p[PF_CHORD_ON] = 1;
    e.parts[GD_KBD2].p[PF_CHORD_LEN] = 2; /* 3 Slots: 0, 4, 7 */
    {
        const int *sl = gd_arp_form_slots(0);
        for (i = 0; i < 8; ++i) e.parts[GD_KBD2].p[PF_SLOT0 + i] = (float)sl[i];
    }
    e.g[GP_TEMPO] = 120.0f;
    gd_engine_update(&e);
    gd_seq_set_playing(&s, 1);
    gd_seq_key(&s, GD_KBD2, 60, 100, 1);
    for (i = 0; i < 44100 * 2 / 64; ++i) { /* 2 s = 1 Takt bei 120 BPM */
        int k;
        const unsigned before = e.parts[GD_KBD2].ageCtr;
        memset(l, 0, sizeof l); memset(r, 0, sizeof r);
        gd_seq_render(&s, l, r, 64);
        notes += (int)(e.parts[GD_KBD2].ageCtr - before);
        for (k = 0; k < GD_MAX_VOICES; ++k) if (e.parts[GD_KBD2].v[k].active && e.parts[GD_KBD2].v[k].held) seen |= 1u << (e.parts[GD_KBD2].v[k].note - 60);
    }
    CHECK(notes >= 15 && notes <= 17, "Arpeggiator: 16 Noten pro Takt erwartet");
    CHECK(seen == ((1u << 0) | (1u << 4) | (1u << 7)), "Arpeggiator: Toene C, E, G erwartet");
    printf("  Arp: %d Noten in einem Takt\n", notes);
}

static void test_presets_load(void)
{
    int i, bad = 0;
    for (i = 0; i < gd_num_presets; ++i) {
        gd_preset_load(&app.seq, i);
        if (app.seq.pat.patLen < 1 || app.eng.g[GP_TEMPO] < 20.0f) ++bad;
    }
    CHECK(bad == 0, "Werkspresets nicht ladbar");
    CHECK(gd_num_presets == 440, "440 Werkspresets erwartet");
    for (i = 0; i < 16; ++i) if (i != GD_ACCENT && i <= 8) CHECK(gd_chan_preset_count(i) > 0 || i == 8, "Kanal-Presets fehlen");
}

int main(void)
{
    test_math();
    test_input();
    test_save_load();
    test_seq_arp();
    test_presets_load();
    if (fails) { printf("%d Test(s) fehlgeschlagen\n", fails); return 1; }
    printf("Alle Tests bestanden\n");
    return 0;
}
