/* GrooveDeck FM-1 - Bildschirm 240 x 240 (RGB565). Zeichnet in einen Streifen (gd_canvas), damit auf dem FM-1
   kein voller Bildspeicher (115 KB) noetig ist: die Plattform ruft gd_ui_draw fuer jeden Streifen auf. */
#include "gd_app.h"
#include "gd_ui_params.h"
#include "gd_math.h"
#include <stdio.h>
#include <string.h>

extern const uint8_t gd_font5x7[95][5];

#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define C_BG RGB(12, 14, 20)
#define C_PANEL RGB(30, 34, 46)
#define C_LINE RGB(60, 66, 84)
#define C_TEXT RGB(230, 232, 238)
#define C_DIM RGB(130, 136, 152)
#define C_ACC RGB(255, 140, 30)
#define C_PLAY RGB(60, 220, 110)
#define C_REC RGB(240, 50, 60)
#define C_STEP_OFF RGB(44, 48, 62)
#define C_BEAT RGB(58, 64, 82)
#define C_CURSOR RGB(255, 255, 255)

static uint16_t part_color(int p)
{
    switch (p) {
        case 0: return RGB(255, 90, 70);
        case 1: return RGB(255, 170, 60);
        case 2: return RGB(240, 220, 70);
        case 3: return RGB(160, 230, 80);
        case 4: return RGB(80, 220, 160);
        case 5: return RGB(70, 200, 240);
        case 6: return RGB(90, 150, 255);
        case 7: return RGB(170, 120, 255);
        case 8: return RGB(220, 110, 240);
        case GD_ACCENT: return RGB(255, 255, 255);
        case GD_KBD1: return RGB(255, 120, 170);
        default: return RGB(120, 240, 230);
    }
}

/* ---------------------------------------------------------- Primitive */
static void fill(gd_canvas *c, int x, int y, int w, int h, uint16_t col)
{
    int yy, xx, y1 = y + h, x1 = x + w;
    if (x < 0) x = 0;
    if (x1 > GD_LCD_W) x1 = GD_LCD_W;
    if (y < c->y0) y = c->y0;
    if (y1 > c->y0 + c->h) y1 = c->y0 + c->h;
    for (yy = y; yy < y1; ++yy) {
        uint16_t *row = c->px + (yy - c->y0) * GD_LCD_W;
        for (xx = x; xx < x1; ++xx) row[xx] = col;
    }
}

static int text(gd_canvas *c, int x, int y, const char *s, uint16_t col, int scale)
{
    if (y >= c->y0 + c->h || y + 8 * scale <= c->y0) return x + (int)strlen(s) * 6 * scale;
    for (; *s; ++s) {
        const int ch = (unsigned char)*s;
        const uint8_t *g = gd_font5x7[(ch < 32 || ch > 126) ? 0 : ch - 32];
        int cx, cy;
        for (cx = 0; cx < 5; ++cx)
            for (cy = 0; cy < 7; ++cy)
                if (g[cx] & (1u << cy)) fill(c, x + cx * scale, y + cy * scale, scale, scale, col);
        x += 6 * scale;
    }
    return x;
}
static int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale - scale; }
static void text_c(gd_canvas *c, int cx, int y, const char *s, uint16_t col, int scale) { text(c, cx - text_w(s, scale) / 2, y, s, col, scale); }

static void note_name(int n, char *b, int cap)
{
    static const char *nn[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    n = gd_clampi(n, 0, 127);
    snprintf(b, (size_t)cap, "%s%d", nn[n % 12], n / 12 - 1);
}

/* ------------------------------------------------------------- Bereiche */
static void draw_header(const gd_app *a, gd_canvas *c)
{
    char b[48];
    const gd_preset_entry *pe = &gd_presets[a->preset];
    fill(c, 0, 0, 240, 18, C_PANEL);
    snprintf(b, sizeof b, "%03d", a->preset + 1);
    text(c, 4, 5, b, C_ACC, 1);
    {
        char nm[26];
        strncpy(nm, pe->name, 25);
        nm[25] = 0;
        text(c, 28, 5, nm, C_TEXT, 1);
    }
    /* Transport */
    if (a->seq.playing) {
        int i;
        for (i = 0; i < 9; ++i) fill(c, 226 + i, 4 + i / 2, 1, 10 - (i / 2) * 2, C_PLAY);
    } else fill(c, 226, 5, 8, 8, C_DIM);
    if (a->rec) fill(c, 214, 6, 7, 7, C_REC);
    snprintf(b, sizeof b, "%s / %s", gd_genre_names[pe->genre], gd_sub_names[pe->genre][pe->sub]);
    text(c, 4, 22, b, C_DIM, 1);
    snprintf(b, sizeof b, "%.1f", (double)a->eng.g[GP_TEMPO]);
    text(c, 236 - text_w(b, 1), 22, b, C_TEXT, 1);
}

static void draw_parts(const gd_app *a, gd_canvas *c)
{
    static const int order[12] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, GD_ACCENT, GD_KBD1, GD_KBD2 };
    int i;
    for (i = 0; i < 12; ++i) {
        const int p = order[i], x = i * 20;
        const int sel = p == a->part, muted = (a->seq.muteMask >> p) & 1u;
        const int has = a->seq.pat.stepMask[p] != 0;
        fill(c, x + 1, 34, 18, 16, sel ? part_color(p) : C_PANEL);
        text_c(c, x + 10, 38, gd_part_short(p), sel ? C_BG : (muted ? C_LINE : (has ? C_TEXT : C_DIM)), 1);
        if (has && !sel) fill(c, x + 1, 50, 18, 2, muted ? C_LINE : part_color(p));
    }
}

static void draw_steps(const gd_app *a, gd_canvas *c)
{
    const gd_pattern *pt = &a->seq.pat;
    const int part = a->part, pl = pt->patLen, base = a->stepPage * 16;
    const int kbd = gd_is_kbd(part);
    const uint16_t pc = part_color(part);
    int i;
    char b[40];
    if (58 >= c->y0 + c->h || 116 <= c->y0) return;
    for (i = 0; i < 16; ++i) {
        const int st = base + i, x = 4 + i * 14 + (i / 4) * 2;
        const int on = st < pl && ((pt->stepMask[part] >> st) & 1u);
        const int acc = st < pl && ((pt->stepMask[GD_ACCENT] >> st) & 1u);
        const int cur = a->seq.playing && a->seq.currentStep == st;
        uint16_t col = st >= pl ? C_BG : (on ? pc : ((i & 3) == 0 ? C_BEAT : C_STEP_OFF));
        if (cur) fill(c, x - 1, 57, 14, 26, C_CURSOR);
        fill(c, x, 58, 12, 24, col);
        if (acc && part != GD_ACCENT && st < pl) fill(c, x + 4, 60, 4, 3, C_BG);
        if (kbd && on) {
            char nn[6];
            const int note = pt->kbdNote[part - GD_KBD1][st];
            note_name(note, nn, sizeof nn);
            nn[2] = 0; /* nur Notenname */
            if (nn[1] != '#') nn[1] = 0;
            text_c(c, x + 6, 86, nn, C_TEXT, 1);
            snprintf(nn, sizeof nn, "%d", note / 12 - 1);
            text_c(c, x + 6, 96, nn, C_DIM, 1);
        }
    }
    snprintf(b, sizeof b, "%s %d-%d/%d", a->stepMode ? "STEP" : "PLAY", base + 1, base + 16 < pl ? base + 16 : pl, pl);
    text(c, 4, 106, b, a->stepMode ? C_ACC : C_PLAY, 1);
    if (!a->stepMode && a->octave) { snprintf(b, sizeof b, "OKT %+d", a->octave); text(c, 120, 106, b, C_TEXT, 1); }
    if (kbd) {
        char nn[8];
        note_name(a->lastNote[part - GD_KBD1], nn, sizeof nn);
        snprintf(b, sizeof b, "NOTE %s", nn);
        text(c, 236 - text_w(b, 1), 106, b, C_DIM, 1);
    }
}

static void draw_knobs(const gd_app *a, gd_canvas *c)
{
    char b[40];
    int k;
    if (118 >= c->y0 + c->h) return;
    fill(c, 0, 118, 240, 14, C_PANEL);
    snprintf(b, sizeof b, "%s %c  -  %s", gd_page_name(a->page), a->sub ? 'B' : 'A', gd_part_short(a->part));
    text(c, 4, 122, b, C_ACC, 1);
    {   /* Soundgoodizer-Anzeige: 12 Baender */
        int i;
        for (i = 0; i < 12; ++i) {
            const int h = gd_roundi(a->eng.good.bands[i] * 10.0f);
            fill(c, 170 + i * 5, 130 - h, 4, h, C_DIM);
        }
    }
    for (k = 0; k < 4; ++k) {
        const gd_pdesc *d = gd_page_param(a->page, a->sub, a->part, k);
        const int x = (k & 1) * 120, y = 136 + (k >> 1) * 52;
        fill(c, x + 2, y, 116, 50, C_PANEL);
        snprintf(b, sizeof b, "%d", k + 1);
        text(c, x + 6, y + 4, b, C_ACC, 1);
        if (!d) { text(c, x + 16, y + 4, "-", C_LINE, 1); continue; }
        text(c, x + 16, y + 4, d->label, C_DIM, 1);
        gd_param_format(a, d, b, sizeof b);
        if (text_w(b, 2) <= 108) text(c, x + 6, y + 17, b, C_TEXT, 2);
        else { b[18] = 0; text(c, x + 6, y + 20, b, C_TEXT, 1); }
        fill(c, x + 6, y + 40, 108, 5, C_STEP_OFF);
        fill(c, x + 6, y + 40, gd_roundi(108.0f * gd_param_norm(a, d)), 5, part_color(a->part));
    }
}

static void draw_message(const gd_app *a, gd_canvas *c)
{
    int w;
    if (a->msgMs <= 0 || !a->msg[0]) return;
    w = text_w(a->msg, 1) + 16;
    if (w > 236) w = 236;
    fill(c, 120 - w / 2, 92, w, 22, C_ACC);
    text_c(c, 120, 99, a->msg, C_BG, 1);
}

void gd_ui_draw(const gd_app *a, gd_canvas *c)
{
    fill(c, 0, c->y0, GD_LCD_W, c->h, C_BG);
    draw_header(a, c);
    draw_parts(a, c);
    draw_steps(a, c);
    draw_knobs(a, c);
    draw_message(a, c);
}
