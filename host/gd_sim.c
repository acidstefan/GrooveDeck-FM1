/* GrooveDeck FM-1 - PC-Simulation der Bedienung ohne Hardware: fuehrt eine Tastenfolge aus,
   schreibt Bildschirmfotos (PPM) und den Ton (WAV). Zum Pruefen der Oberflaeche ohne Emulator.
   gd_sim [ausgabeordner] */
#include "gd_app.h"
#include "gd_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100
static gd_app app;
static uint16_t strip[16 * GD_LCD_W];
static FILE *wav;
static long frames;
static char outdir[256] = ".";

static void shot_panel(const char *name, uint32_t now);
static void shot(const char *name)
{
    char path[400];
    FILE *f;
    int y0, y, x;
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", GD_LCD_W, GD_LCD_H);
    for (y0 = 0; y0 < GD_LCD_H; y0 += 16) {          /* streifenweise wie auf dem Geraet */
        gd_canvas c;
        c.px = strip; c.y0 = y0; c.h = 16;
        gd_ui_draw(&app, &c);
        for (y = 0; y < 16; ++y)
            for (x = 0; x < GD_LCD_W; ++x) {
                const uint16_t p = strip[y * GD_LCD_W + x];
                fputc((p >> 8) & 0xF8, f); fputc((p >> 3) & 0xFC, f); fputc((p << 3) & 0xF8, f);
            }
    }
    fclose(f);
    shot_panel(name, app.audioMs);
    printf("Bild: %s\n", path);
}

/* Bedienfeld mit LEDs (schematisch, keine Nachbildung des Gehaeuses): Buttons oben, 27 Tasten unten */
extern const uint8_t gd_font5x7[95][5];
#define PW 600
#define PH 150
static uint8_t panel[PH][PW][3];
static void prect(int x, int y, int w, int h, int r, int g, int b)
{
    int i, j;
    for (j = y; j < y + h && j < PH; ++j) for (i = x; i < x + w && i < PW; ++i) { panel[j][i][0] = (uint8_t)r; panel[j][i][1] = (uint8_t)g; panel[j][i][2] = (uint8_t)b; }
}
static void ptext(int x, int y, const char *t)
{
    for (; *t; ++t, x += 6) {
        const uint8_t *g = gd_font5x7[(*t < 32 || *t > 126) ? 0 : *t - 32];
        int cx, cy;
        for (cx = 0; cx < 5; ++cx) for (cy = 0; cy < 7; ++cy) if (g[cx] & (1u << cy)) prect(x + cx, y + cy, 1, 1, 200, 200, 210);
    }
}
static void led(int x, int y, int w, int v, int r, int g, int b)
{
    prect(x, y, w, 4, 50 + (r - 50) * v / 255, 50 + (g - 50) * v / 255, 56 + (b - 56) * v / 255);
}
static void shot_panel(const char *name, uint32_t now)
{
    static const char *bn[GB_COUNT] = { "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+" };
    static const int order[GB_COUNT] = { GB_FX, GB_SEL, GB_ENV, GB_LFO, GB_EDIT, GB_GLO, GB_OCTDN, GB_HOME, GB_SAVE, GB_ARP, GB_SEQ, GB_PLAY, GB_REC, GB_OCTUP };
    uint8_t keys[GD_KEYS], btn[GB_COUNT], green;
    char path[400];
    FILE *f;
    int i;
    gd_leds_compute(&app, now, keys, btn, &green);
    prect(0, 0, PW, PH, 24, 26, 32);
    for (i = 0; i < GB_COUNT; ++i) {
        const int b = order[i], x = 8 + (i % 7) * 84, y = 6 + (i / 7) * 34;
        prect(x, y, 76, 28, 44, 46, 56);
        ptext(x + 6, y + 6, bn[b]);
        if (b == GB_PLAY) { led(x + 4, y + 20, 32, btn[b], 255, 255, 255); led(x + 40, y + 20, 32, green, 60, 230, 90); }
        else if (b == GB_REC) led(x + 4, y + 20, 68, btn[b], 255, 40, 40);
        else led(x + 4, y + 20, 68, btn[b], 255, 255, 255);
    }
    for (i = 0; i < GD_KEYS; ++i) {           /* Klaviatur: weisse Tasten unten, schwarze oben dazwischen */
        const int w = gd_key_white(i), b = gd_key_black(i);
        char nr[4];
        if (w >= 0) {
            const int x = 8 + w * 36;
            prect(x, 112, 32, 34, 70, 72, 82);
            led(x + 3, 116, 26, keys[i], 255, 255, 255);
            snprintf(nr, sizeof nr, "%d", w + 1);
            ptext(x + (w < 9 ? 12 : 8), 130, nr);
        } else {
            const int x = 8 + gd_key_white(i - 1) * 36 + 22;
            prect(x, 78, 28, 30, 40, 42, 50);
            led(x + 3, 82, 22, keys[i], 255, 255, 255);
            snprintf(nr, sizeof nr, "%c", "1234567890X"[b]);
            ptext(x + 10, 94, nr);
        }
    }
    snprintf(path, sizeof path, "%s/%s_panel.ppm", outdir, name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", PW, PH);
    fwrite(panel, 1, sizeof panel, f);
    fclose(f);
}

static void run(double secs)
{
    float l[256], r[256];
    long n = (long)(secs * SR);
    while (n > 0) {
        const int c = n > 256 ? 256 : (int)n;
        int i;
        gd_app_audio(&app, l, r, c);
        for (i = 0; i < c; ++i) {
            const int a = (int)(gd_clampf(l[i], -1, 1) * 32767), b = (int)(gd_clampf(r[i], -1, 1) * 32767);
            fputc(a & 255, wav); fputc((a >> 8) & 255, wav); fputc(b & 255, wav); fputc((b >> 8) & 255, wav);
        }
        frames += c;
        n -= c;
        gd_app_tick(&app, c * 1000 / SR);
    }
}
static void btn(int b) { gd_app_post_button(&app, b, 1); gd_app_post_button(&app, b, 0); }
static void key(int k, double hold) { gd_app_post_key(&app, k, 1); run(hold); gd_app_post_key(&app, k, 0); }
static void enc(int e, int d) { gd_app_post_encoder(&app, e, d); }

static void w32(unsigned v) { fputc(v & 255, wav); fputc((v >> 8) & 255, wav); fputc((v >> 16) & 255, wav); fputc(v >> 24, wav); }

int main(int argc, char **argv)
{
    char path[400];
    if (argc > 1) snprintf(outdir, sizeof outdir, "%s", argv[1]);
    snprintf(path, sizeof path, "%s/sim.wav", outdir);
    wav = fopen(path, "wb");
    if (!wav) { perror(path); return 1; }
    fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wav);
    w32(16); fputc(1, wav); fputc(0, wav); fputc(2, wav); fputc(0, wav); w32(SR); w32(SR * 4); fputc(4, wav); fputc(0, wav); fputc(16, wav); fputc(0, wav);
    fwrite("data\0\0\0\0", 1, 8, wav);

    gd_app_init(&app, SR);
    gd_app_set_battery(&app, 72, 0);
    run(0.1);
    shot("01_start");
    btn(GB_PLAY); run(2.0);
    shot("02_play_home");
    enc(GE_SELECT, 10);                      /* Part: Bass */
    btn(GB_ENV); run(0.05);
    enc(GE_K1, 12); enc(GE_K2, 20); run(1.5);
    shot("03_bass_env");
    btn(GB_EDIT); run(0.05); shot("04_bass_edit");
    btn(GB_EDIT); run(0.05); shot("05_bass_edit_b");
    enc(GE_SELECT, 1); btn(GB_ARP); run(0.05);
    enc(GE_K1, 1); enc(GE_K2, 9); run(0.05);  /* Lead: Arp an, Style */
    btn(GB_SEQ); run(0.05);                  /* PLAY-Modus: Tasten = Noten */
    gd_app_post_key(&app, 12, 1); run(0.45); shot("06_lead_arp"); gd_app_post_key(&app, 12, 0); run(0.2);
    btn(GB_SEQ); enc(GE_SELECT, -11); run(0.05); /* zurueck auf BD, STEP */
    key(gd_white_key(2), 0.05); key(gd_white_key(6), 0.05); key(gd_white_key(10), 0.05);  /* Steps 3, 7, 11 */
    btn(GB_GLO); enc(GE_K2, 16); run(0.05);   /* Laenge 32 */
    key(gd_black_key(BK_PAGE2), 0.05); run(1.0);
    shot("07_glo_page2");
    btn(GB_FX); run(0.5); shot("08_fx");
    btn(GB_SEL); enc(GE_K1, 3); run(0.8); shot("09_kanal_preset");
    enc(GE_PRESETS, 57); run(0.8); shot("10_preset58");
    run(3.0);
    gd_app_post_button(&app, GB_SAVE, 1); run(1.0); shot("11_saved"); gd_app_post_button(&app, GB_SAVE, 0);
    btn(GB_PLAY); run(0.5);

    fseek(wav, 4, SEEK_SET); w32((unsigned)(36 + frames * 4));
    fseek(wav, 40, SEEK_SET); w32((unsigned)(frames * 4));
    fclose(wav);
    printf("Ton: %s (%.1f s), Benutzerstand %d Byte\n", path, (double)frames / SR, app.userLen);
    return 0;
}
