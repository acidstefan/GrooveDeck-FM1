/* GrooveDeck FM-1 - Bedienfeld am PC: Nachbildung des FM-1-Layouts mit Display, LEDs, Tasten, Buttons,
   Drehreglern und MASTER. Laeuft mit derselben Kern-Software wie die Firmware (gd_app), aber nativ am PC:
   voller Klang in Echtzeit, ohne Emulator. Zoom stufenlos.

   Bedienung
     Maus      Tasten/Buttons anklicken (halten = gedrueckt halten), Regler: Mausrad darueber oder ziehen
     Tastatur  Klaviatur: weisse Tasten Z X C V B N M , . / Q W E R T Y, schwarze darueber (S D F H J L ; ' 3 4 6)
               F1 FX  F2 SEL  F3 ENV  F4 LFO  F5 EDIT  F6 GLO  F7 HOME  F8 SAVE  F9 ARP  F10 SEQ
               F11 oder Leertaste PLAY  F12 REC   [ OCT-   ] OCT+
               Pfeil hoch/runter = SELECT, Pfeil links/rechts = PRESETS
     Zoom      Strg + Mausrad, Strg + Plus/Minus, Strg + 0 = 100 %; Fenster frei ziehbar

   gd_panel [--zoom 1.0] [--test ausgabe.ppm]   (--test: ohne Fenster, klickt eine Folge und speichert ein Bild) */
#include "gd_app.h"
#include "gd_input.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100
#define PW 1500
#define PH 900

extern const uint8_t gd_font5x7[95][5];

static gd_app app;
static uint32_t px[PH * PW];
static uint16_t strip[16 * GD_LCD_W];
static uint32_t lcd[GD_LCD_H * GD_LCD_W];

/* ------------------------------------------------------------------ Layout */
typedef struct { int x, y, w, h; } rect;
enum { C_KEY, C_BTN, C_ENC, C_MASTER };
typedef struct { int kind, id; rect r; const char *label; } control;

static const rect LCD_R = { 420, 50, 380, 380 };
static control ctl[64];
static int nctl;

static void add(int kind, int id, int x, int y, int w, int h, const char *label)
{
    control *c = &ctl[nctl++];
    c->kind = kind; c->id = id; c->r.x = x; c->r.y = y; c->r.w = w; c->r.h = h; c->label = label;
}

static void layout(void)
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
    int k, n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    if (n == 0) n = 1;
    for (k = 0; k <= n; ++k) disc(x0 + (x1 - x0) * k / n, y0 + (y1 - y0) * k / n, wdt, c);
}
static int text_w(const char *s, int sc) { return (int)strlen(s) * 6 * sc - sc; }
static void text(int x, int y, const char *s, int sc, uint32_t c)
{
    for (; *s; ++s, x += 6 * sc) {
        const uint8_t *g = gd_font5x7[(*s < 32 || *s > 126) ? 0 : *s - 32];
        int cx, cy;
        for (cx = 0; cx < 5; ++cx) for (cy = 0; cy < 7; ++cy) if (g[cx] & (1u << cy)) box(x + cx * sc, y + cy * sc, sc, sc, c);
    }
}
static void text_c(int cx, int y, const char *s, int sc, uint32_t c) { text(cx - text_w(s, sc) / 2, y, s, sc, c); }

static int pressed[64];          /* je Control: gedrueckt (Maus oder Tastatur) */
static float encAngle[GE_COUNT];
static float master = 0.8f;

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

static void draw_panel(uint32_t nowMs)
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
                snprintf(n, sizeof n, "%d", gd_key_white(c->id) + 1);
                text_c(r.x + r.w / 2, r.y + r.h - 60, n, 2, rgb(120, 122, 130));
            } else {
                char n[4];
                snprintf(n, sizeof n, "%d", gd_key_black(c->id) + 1);
                text_c(r.x + r.w / 2, r.y + 14, n, 2, rgb(150, 152, 160));
            }
        } else {
            const int cx = r.x + r.w / 2, cy = r.y + r.h / 2, rad = r.w / 2;
            float ang = c->kind == C_MASTER ? (-135.0f + 270.0f * master) : encAngle[c->id];
            const float a = (ang - 90.0f) * 3.14159265f / 180.0f;
            disc(cx, cy, rad, rgb(14, 14, 16));
            disc(cx, cy, rad - 6, pressed[i] ? rgb(80, 82, 92) : rgb(62, 64, 72));
            line(cx, cy, cx + (int)((rad - 14) * cosf(a)), cy + (int)((rad - 14) * sinf(a)), 3, rgb(240, 240, 245));
            text_c(cx, r.y + r.h + 10, c->label, 2, rgb(200, 202, 210));
            if (c->kind == C_MASTER) {
                char m[16];
                snprintf(m, sizeof m, "%d %%", (int)(master * 100.0f + 0.5f));
                text_c(cx, r.y + r.h + 32, m, 2, rgb(140, 142, 150));
            }
        }
    }
    text(40, 880, "Maus: klicken/halten, Regler: Rad oder ziehen   Strg+Rad / Strg +/-: Zoom   Hilfe: siehe Kopf von host/gd_panel.c",
         1, rgb(120, 122, 130));
}

/* ------------------------------------------------------------------ Bedienung */
static void press(int i, int down)
{
    const control *c = &ctl[i];
    if (pressed[i] == down) return;
    pressed[i] = down;
    if (c->kind == C_KEY) gd_app_post_key(&app, c->id, down);
    else if (c->kind == C_BTN) gd_app_post_button(&app, c->id, down);
}
static void turn(int enc, int delta)
{
    encAngle[enc] += 18.0f * (float)delta;
    gd_app_post_encoder(&app, enc, delta);
}
static void set_master(float v)
{
    master = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    gd_app_post_master(&app, master);
}
static int hit(int x, int y)
{
    int i;
    for (i = nctl - 1; i >= 0; --i)   /* schwarze Tasten liegen ueber den weissen: zuletzt angelegt, zuerst geprueft */
        if (x >= ctl[i].r.x && x < ctl[i].r.x + ctl[i].r.w && y >= ctl[i].r.y && y < ctl[i].r.y + ctl[i].r.h) return i;
    return -1;
}
static int find(int kind, int id)
{
    int i;
    for (i = 0; i < nctl; ++i) if (ctl[i].kind == kind && ctl[i].id == id) return i;
    return -1;
}

static int key_for_scancode(SDL_Scancode s)
{
    static const SDL_Scancode white[16] = {
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N,
        SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y };
    static const SDL_Scancode black[11] = {
        SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_L,
        SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_6 };
    int i;
    for (i = 0; i < 16; ++i) if (s == white[i]) return find(C_KEY, gd_white_key(i));
    for (i = 0; i < 11; ++i) if (s == black[i]) return find(C_KEY, gd_black_key(i));
    switch (s) {
        case SDL_SCANCODE_F1: return find(C_BTN, GB_FX);
        case SDL_SCANCODE_F2: return find(C_BTN, GB_SEL);
        case SDL_SCANCODE_F3: return find(C_BTN, GB_ENV);
        case SDL_SCANCODE_F4: return find(C_BTN, GB_LFO);
        case SDL_SCANCODE_F5: return find(C_BTN, GB_EDIT);
        case SDL_SCANCODE_F6: return find(C_BTN, GB_GLO);
        case SDL_SCANCODE_F7: return find(C_BTN, GB_HOME);
        case SDL_SCANCODE_F8: return find(C_BTN, GB_SAVE);
        case SDL_SCANCODE_F9: return find(C_BTN, GB_ARP);
        case SDL_SCANCODE_F10: return find(C_BTN, GB_SEQ);
        case SDL_SCANCODE_F11: case SDL_SCANCODE_SPACE: return find(C_BTN, GB_PLAY);
        case SDL_SCANCODE_F12: return find(C_BTN, GB_REC);
        case SDL_SCANCODE_LEFTBRACKET: return find(C_BTN, GB_OCTDN);
        case SDL_SCANCODE_RIGHTBRACKET: return find(C_BTN, GB_OCTUP);
        default: return -1;
    }
}

/* ------------------------------------------------------------------ Audio */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    float *out = (float *)stream, l[64], r[64];
    int n = len / (int)(2 * sizeof(float)), done = 0, i;
    uint8_t mo[64];
    (void)ud;
    while (done < n) {
        const int c = n - done < 64 ? n - done : 64;
        gd_app_audio(&app, l, r, c);
        for (i = 0; i < c; ++i) {
            out[2 * (done + i)] = l[i] < -1.0f ? -1.0f : l[i] > 1.0f ? 1.0f : l[i];
            out[2 * (done + i) + 1] = r[i] < -1.0f ? -1.0f : r[i] > 1.0f ? 1.0f : r[i];
        }
        done += c;
    }
    while (gd_app_midi_out_read(&app, mo, sizeof mo) > 0) { } /* MIDI-Ausgang: am PC nicht verbunden */
}

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", PW, PH);
    for (i = 0; i < PW * PH; ++i) { fputc((px[i] >> 16) & 255, f); fputc((px[i] >> 8) & 255, f); fputc(px[i] & 255, f); }
    fclose(f);
}

/* --test: Ablauf ohne Fenster und ohne Ton; Bedienung wie per Maus, Bild am Ende */
static void run_audio(double secs)
{
    float l[64], r[64];
    long k, n = (long)(secs * SR / 64);
    uint8_t mo[64];
    for (k = 0; k < n; ++k) { gd_app_audio(&app, l, r, 64); gd_app_tick(&app, 1); while (gd_app_midi_out_read(&app, mo, 64) > 0) { } }
}
static void click(int i, double hold) { press(i, 1); run_audio(hold); press(i, 0); run_audio(0.05); }
static int test_run(const char *out)
{
    gd_app_init(&app, SR);
    gd_app_set_battery(&app, 72, 0);
    run_audio(0.2);
    click(find(C_KEY, gd_white_key(0)), 0.05);
    click(find(C_KEY, gd_white_key(4)), 0.05);
    click(find(C_KEY, gd_white_key(8)), 0.05);
    click(find(C_BTN, GB_PLAY), 0.05);
    turn(GE_K1, 3);
    set_master(0.7f);
    run_audio(1.3);
    draw_panel(app.audioMs);
    save_ppm(out);
    printf("Bild: %s\n", out);
    return 0;
}

int main(int argc, char **argv)
{
    float zoom = 1.0f;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
    int quit = 0, drag = -1, dragY = 0, i;
    int keyOf[SDL_NUM_SCANCODES];
    for (i = 0; i < SDL_NUM_SCANCODES; ++i) keyOf[i] = -1;
    layout();
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--test") && i + 1 < argc) return test_run(argv[i + 1]);
        if (!strcmp(argv[i], "--zoom") && i + 1 < argc) zoom = (float)atof(argv[++i]);
    }
    if (zoom < 0.3f) zoom = 0.3f;
    if (zoom > 3.0f) zoom = 3.0f;
    gd_app_init(&app, SR);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    win = SDL_CreateWindow("GrooveDeck FM-1", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           (int)(PW * zoom), (int)(PH * zoom), SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren) ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, PW, PH);          /* Zoom = Fenstergroesse; Mauskoordinaten werden umgerechnet */
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PW, PH);

    SDL_zero(want);
    want.freq = SR; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 512; want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) fprintf(stderr, "Kein Ton: %s (Bedienung geht trotzdem)\n", SDL_GetError());
    else SDL_PauseAudioDevice(dev, 0);

    while (!quit) {
        SDL_Event e;
        static uint32_t lastTick;
        const uint32_t now = SDL_GetTicks();
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: quit = 1; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const int h = hit(e.button.x, e.button.y);
                        if (h < 0) break;
                        if (ctl[h].kind == C_KEY || ctl[h].kind == C_BTN) { SDL_LockAudioDevice(dev); press(h, 1); SDL_UnlockAudioDevice(dev); }
                        else { pressed[h] = 1; }
                        drag = h; dragY = e.button.y;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT && drag >= 0) {
                        if (ctl[drag].kind == C_KEY || ctl[drag].kind == C_BTN) { SDL_LockAudioDevice(dev); press(drag, 0); SDL_UnlockAudioDevice(dev); }
                        else pressed[drag] = 0;
                        drag = -1;
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (drag >= 0 && ctl[drag].kind == C_ENC) {
                        while (e.motion.y <= dragY - 12) { turn(ctl[drag].id, +1); dragY -= 12; }
                        while (e.motion.y >= dragY + 12) { turn(ctl[drag].id, -1); dragY += 12; }
                    } else if (drag >= 0 && ctl[drag].kind == C_MASTER) {
                        set_master(master - (float)(e.motion.y - dragY) / 300.0f);
                        dragY = e.motion.y;
                    }
                    break;
                case SDL_MOUSEWHEEL: {
                    int mx, my, lx, ly, h;
                    if (SDL_GetModState() & KMOD_CTRL) { zoom *= e.wheel.y > 0 ? 1.1f : 1.0f / 1.1f; goto rezoom; }
                    SDL_GetMouseState(&mx, &my);
                    {   /* Fenster- in Panel-Koordinaten */
                        int ww, wh;
                        float sx, sy;
                        SDL_GetWindowSize(win, &ww, &wh);
                        sx = (float)PW / (float)ww; sy = (float)PH / (float)wh;
                        if (sx < sy) sx = sy;
                        lx = (int)((mx - (ww - PW / sx) / 2) * sx); ly = (int)((my - (wh - PH / sx) / 2) * sx);
                    }
                    h = hit(lx, ly);
                    if (h >= 0 && ctl[h].kind == C_ENC) turn(ctl[h].id, e.wheel.y > 0 ? 1 : -1);
                    else if (h >= 0 && ctl[h].kind == C_MASTER) set_master(master + (e.wheel.y > 0 ? 0.02f : -0.02f));
                    break;
                }
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    const int down = e.type == SDL_KEYDOWN;
                    const SDL_Scancode s = e.key.keysym.scancode;
                    if (down && (e.key.keysym.mod & KMOD_CTRL)) {
                        if (s == SDL_SCANCODE_EQUALS || s == SDL_SCANCODE_KP_PLUS || s == SDL_SCANCODE_RIGHTBRACKET) { zoom *= 1.1f; goto rezoom; }
                        if (s == SDL_SCANCODE_MINUS || s == SDL_SCANCODE_KP_MINUS || s == SDL_SCANCODE_SLASH) { zoom /= 1.1f; goto rezoom; }
                        if (s == SDL_SCANCODE_0 || s == SDL_SCANCODE_KP_0) { zoom = 1.0f; goto rezoom; }
                        break;
                    }
                    if (e.key.repeat) break;
                    if (down && s == SDL_SCANCODE_UP) { turn(GE_SELECT, 1); break; }
                    if (down && s == SDL_SCANCODE_DOWN) { turn(GE_SELECT, -1); break; }
                    if (down && s == SDL_SCANCODE_RIGHT) { turn(GE_PRESETS, 1); break; }
                    if (down && s == SDL_SCANCODE_LEFT) { turn(GE_PRESETS, -1); break; }
                    if (down) keyOf[s] = key_for_scancode(s);
                    if (keyOf[s] >= 0) { SDL_LockAudioDevice(dev); press(keyOf[s], down); SDL_UnlockAudioDevice(dev); }
                    if (!down) keyOf[s] = -1;
                    break;
                }
                default: break;
            }
            continue;
        rezoom:
            if (zoom < 0.3f) zoom = 0.3f;
            if (zoom > 3.0f) zoom = 3.0f;
            SDL_SetWindowSize(win, (int)(PW * zoom), (int)(PH * zoom));
        }
        if (!lastTick) lastTick = now;
        gd_app_tick(&app, (int)(now - lastTick));
        lastTick = now;
        draw_panel(app.audioMs);
        SDL_UpdateTexture(tex, NULL, px, PW * 4);
        SDL_SetRenderDrawColor(ren, 20, 21, 25, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(10);
    }
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
