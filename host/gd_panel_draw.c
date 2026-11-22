/* GrooveDeck FM-1 - Bedienfeld: Layout, Zeichnen und Bedienung, gemeinsam fuer gd_panel (SDL) und die
   Browser-Fassung (web/). Zeichnet in den Puffer px (PW x PH, ARGB). Keine libc ausser string.h noetig. */
#include "gd_panel_draw.h"
#include "gd_fmt.h"

/* Sinus/Kosinus fuer die Reglerzeiger (Taylor nach Reduktion auf -pi..pi, genau genug fuers Bild) */
static float gd_sinf(float x)
{
    const float pi = 3.14159265f;
    float x2;
    while (x > pi) x -= 2.0f * pi;
    while (x < -pi) x += 2.0f * pi;
    if (x > pi / 2) x = pi - x;
    else if (x < -pi / 2) x = -pi - x;
    x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f)));
}
static float gd_cosf(float x) { return gd_sinf(x + 1.57079633f); }

extern const uint8_t gd_font5x7[95][5];

gd_app app;
uint32_t px[PH * PW];
static uint16_t strip[16 * GD_LCD_W];
static uint32_t lcd[GD_LCD_H * GD_LCD_W];

/* ------------------------------------------------------------------ Layout */

static const rect LCD_R = { 420, 50, 380, 380 };
control ctl[64];
int nctl;

static void add(int kind, int id, int x, int y, int w, int h, const char *label)
{
    control *c = &ctl[nctl++];
    c->kind = kind; c->id = id; c->r.x = x; c->r.y = y; c->r.w = w; c->r.h = h; c->label = label;
}

void layout(void)
{
    static const char *bl[12] = { "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC" };
    static const int bo[12] = { GB_FX, GB_SEL, GB_ENV, GB_LFO, GB_EDIT, GB_GLO, GB_HOME, GB_SAVE, GB_ARP, GB_SEQ, GB_PLAY, GB_REC };
    static const char *kl[4] = { "1", "2", "3", "4" };
    int i;
    nctl = 0;
    /* links: MASTER, SELECT, PRESETS, ALGORITHM */
    add(C_MASTER, 0, 60, 70, 120, 120, "MASTER");
    add(C_ENC, GE_SELECT, 230, 70, 120, 120, "SELECT");
    add(C_ENC, GE_PRESETS, 60, 260, 120, 120, "PRESETS");
    add(C_ENC, GE_ALGO, 230, 260, 120, 120, "ALGORITHM");
    /* rechts: Regler 1-4, darunter 12 Buttons */
    for (i = 0; i < 4; ++i) add(C_ENC, GE_K1 + i, 870 + i * 150, 50, 110, 110, kl[i]);
    for (i = 0; i < 12; ++i) add(C_BTN, bo[i], 860 + (i % 6) * 103, 230 + (i / 6) * 100, 90, 80, bl[i]);
    /* Oktave */
    add(C_BTN, GB_OCTDN, 60, 470, 130, 56, "OCT-");
    add(C_BTN, GB_OCTUP, 220, 470, 130, 56, "OCT+");
    /* Klaviatur: 16 weisse, 11 schwarze */
    for (i = 0; i < GD_KEYS; ++i) {
        const int w = gd_key_white(i);
        if (w >= 0) add(C_KEY, i, 40 + w * 88, 600, 84, 270, NULL);
    }
    for (i = 0; i < GD_KEYS; ++i) {
        const int b = gd_key_black(i);
        if (b >= 0) add(C_KEY, i, 40 + gd_key_white(i - 1) * 88 + 60, 600, 56, 160, NULL);
    }
}

/* ------------------------------------------------------------------ Zeichnen */
static uint32_t rgb(int r, int g, int b) { return 0xff000000u | (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b; }
static uint32_t mix(uint32_t a, uint32_t b, int t) /* t 0..255 */
{
    const int ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255;
    const int rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
    return rgb(ra + (rb - ra) * t / 255, ga + (gb - ga) * t / 255, ba + (bb - ba) * t / 255);
}
static void box(int x, int y, int w, int h, uint32_t c)
{
    int i, j;
    for (j = y < 0 ? 0 : y; j < y + h && j < PH; ++j)
        for (i = x < 0 ? 0 : x; i < x + w && i < PW; ++i) px[j * PW + i] = c;
}
static void rbox(int x, int y, int w, int h, int rad, uint32_t c)   /* abgerundet */
{
    int i, j;
    for (j = 0; j < h; ++j)
        for (i = 0; i < w; ++i) {
            int dx = i < rad ? rad - i : i >= w - rad ? i - (w - rad - 1) : 0;
            int dy = j < rad ? rad - j : j >= h - rad ? j - (h - rad - 1) : 0;
            if (dx * dx + dy * dy > rad * rad) continue;
            if (x + i >= 0 && x + i < PW && y + j >= 0 && y + j < PH) px[(y + j) * PW + x + i] = c;
        }
}
static void disc(int cx, int cy, int rad, uint32_t c)
{
    int i, j;
    for (j = -rad; j <= rad; ++j)
        for (i = -rad; i <= rad; ++i)
            if (i * i + j * j <= rad * rad && cx + i >= 0 && cx + i < PW && cy + j >= 0 && cy + j < PH) px[(cy + j) * PW + cx + i] = c;
}
static void line(int x0, int y0, int x1, int y1, int wdt, uint32_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1, k, n = dx > dy ? dx : dy;
    if (n == 0) n = 1;
    for (k = 0; k <= n; ++k) disc(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n, wdt, c);
}
static int text_w(const char *s, int sc) { int n = 0; while (s[n]) ++n; return n * 6 * sc - sc; }
static void text(int x, int y, const char *s, int sc, uint32_t c)
{
    for (; *s; ++s, x += 6 * sc) {
        const uint8_t *g = gd_font5x7[(*s < 32 || *s > 126) ? 0 : *s - 32];
        int cx, cy;
        for (cx = 0; cx < 5; ++cx) for (cy = 0; cy < 7; ++cy) if (g[cx] & (1u << cy)) box(x + cx * sc, y + cy * sc, sc, sc, c);
    }
}
static void text_c(int cx, int y, const char *s, int sc, uint32_t c) { text(cx - text_w(s, sc) / 2, y, s, sc, c); }

int pressed[64];          /* je Control: gedrueckt (Maus oder Tastatur) */
static float encAngle[GE_COUNT];
float master = 0.8f;

static void draw_lcd(void)
{
    int y0, y, x;
    for (y0 = 0; y0 < GD_LCD_H; y0 += 16) {
        gd_canvas c;
        c.px = strip; c.y0 = y0; c.h = 16;
        gd_ui_draw(&app, &c);
        for (y = 0; y < 16; ++y)
            for (x = 0; x < GD_LCD_W; ++x) {
                const uint16_t p = strip[y * GD_LCD_W + x];
                lcd[(y0 + y) * GD_LCD_W + x] = rgb((p >> 8) & 0xF8, (p >> 3) & 0xFC, (p << 3) & 0xF8);
            }
    }
    /* in den Panel-Puffer, nachstehend punktgenau skaliert */
    for (y = 0; y < LCD_R.h; ++y)
        for (x = 0; x < LCD_R.w; ++x)
            px[(LCD_R.y + y) * PW + LCD_R.x + x] = lcd[(y * GD_LCD_H / LCD_R.h) * GD_LCD_W + x * GD_LCD_W / LCD_R.w];
}

void draw_panel(uint32_t nowMs)
{
    uint8_t keys[GD_KEYS], btn[GB_COUNT], green;
    int i;
    gd_leds_compute(&app, nowMs, keys, btn, &green);
    box(0, 0, PW, PH, rgb(30, 32, 38));
    rbox(LCD_R.x - 14, LCD_R.y - 14, LCD_R.w + 28, LCD_R.h + 28, 12, rgb(12, 12, 14));
    draw_lcd();
    text(LCD_R.x, LCD_R.y + LCD_R.h + 24, "GrooveDeck FM-1", 2, rgb(120, 124, 136));
    for (i = 0; i < nctl; ++i) {
        const control *c = &ctl[i];
        const rect r = c->r;
        if (c->kind == C_BTN) {
            const int lv = btn[c->id];
            uint32_t base = pressed[i] ? rgb(70, 72, 82) : rgb(52, 54, 62);
            rbox(r.x, r.y, r.w, r.h, 10, base);
            text_c(r.x + r.w / 2, r.y + 14, c->label, 2, rgb(215, 218, 226));
            if (c->id == GB_PLAY) {
                rbox(r.x + 10, r.y + r.h - 22, r.w / 2 - 14, 10, 4, mix(rgb(40, 42, 48), rgb(255, 255, 255), lv));
                rbox(r.x + r.w / 2 + 4, r.y + r.h - 22, r.w / 2 - 14, 10, 4, mix(rgb(40, 42, 48), rgb(60, 230, 90), green));
            } else
                rbox(r.x + 10, r.y + r.h - 22, r.w - 20, 10, 4,
                     mix(rgb(40, 42, 48), c->id == GB_REC ? rgb(255, 50, 50) : rgb(255, 255, 255), lv));
        } else if (c->kind == C_KEY) {
            const int white = gd_key_white(c->id) >= 0, lv = keys[c->id % GD_KEYS];
            uint32_t base = white ? (pressed[i] ? rgb(200, 202, 208) : rgb(232, 233, 236))
                                  : (pressed[i] ? rgb(70, 72, 80) : rgb(28, 29, 33));
            rbox(r.x, r.y, r.w, r.h, 8, rgb(10, 10, 12));
            rbox(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 7, base);
            rbox(r.x + r.w / 2 - 12, r.y + r.h - 34, 24, 14, 5, mix(white ? rgb(190, 192, 198) : rgb(55, 56, 62), rgb(255, 120, 40), lv));
            if (white) {
                char n[4];
                gd_snprintf(n, sizeof n, "%d", gd_key_white(c->id) + 1);
                text_c(r.x + r.w / 2, r.y + r.h - 60, n, 2, rgb(120, 122, 130));
            } else {   /* STEP-Modus: schwarze Tasten = Kanaele */
                static const char *cn[11] = { "BD", "SD", "TM", "PC", "CP", "CH", "OH", "CY", "C2", "BS", "LD" };
                text_c(r.x + r.w / 2, r.y + 14, cn[gd_key_black(c->id)], 2, rgb(150, 152, 160));
            }
        } else {
            const int cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2;
            float ang = c->kind == C_MASTER ? (-135.0f + 270.0f * master) : encAngle[c->id];
            const float a = (ang - 90.0f) * 3.14159265f / 180.0f;
            disc(cx, cy, rad, rgb(14, 14, 16));
            disc(cx, cy, rad - 6, pressed[i] ? rgb(80, 82, 92) : rgb(62, 64, 72));
            line(cx, cy, cx + (int)((rad - 14) * gd_cosf(a)), cy + (int)((rad - 14) * gd_sinf(a)), 3, rgb(240, 240, 245));
            text_c(cx, r.y + r.h + 10, c->label, 2, rgb(200, 202, 210));
            if (c->kind == C_MASTER) {
                char m[16];
                gd_snprintf(m, sizeof m, "%d %%", (int)(master * 100.0f + 0.5f));
                text_c(cx, r.y + r.h + 32, m, 2, rgb(140, 142, 150));
            }
        }
    }
    text(40, 880, GD_PANEL_HELP,
         1, rgb(120, 122, 130));
}

/* ------------------------------------------------------------------ Bedienung */
void press(int i, int down)
{
    const control *c = &ctl[i];
    if (pressed[i] == down) return;
    pressed[i] = down;
    if (c->kind == C_KEY) gd_app_post_key(&app, c->id, down);
    else if (c->kind == C_BTN) gd_app_post_button(&app, c->id, down);
}
void turn(int enc, int delta)
{
    encAngle[enc] += 18.0f * (float)delta;
    gd_app_post_encoder(&app, enc, delta);
}
void set_master(float v)
{
    master = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    gd_app_post_master(&app, master);
}
int hit(int x, int y)
{
    int i;
    for (i = nctl - 1; i >= 0; --i)   /* schwarze Tasten liegen ueber den weissen: zuletzt angelegt, zuerst geprueft */
        if (x >= ctl[i].r.x && x < ctl[i].r.x + ctl[i].r.w && y >= ctl[i].r.y && y < ctl[i].r.y + ctl[i].r.h) return i;
    return -1;
}
int find(int kind, int id)
{
    int i;
    for (i = 0; i < nctl; ++i) if (ctl[i].kind == kind && ctl[i].id == id) return i;
    return -1;
}

