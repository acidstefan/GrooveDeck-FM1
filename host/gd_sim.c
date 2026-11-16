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
    printf("Bild: %s\n", path);
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
    key(12, 0.6); key(16, 0.6);
    shot("06_lead_arp");
    btn(GB_SEQ); enc(GE_SELECT, -11); run(0.05); /* zurueck auf BD, STEP */
    key(2, 0.05); key(6, 0.05); key(10, 0.05);  /* Steps setzen */
    btn(GB_GLO); enc(GE_K2, 16); run(0.05);   /* Laenge 32 */
    btn(GB_OCTUP); run(1.0);
    shot("07_glo_page2");
    btn(GB_FX); run(0.5); shot("08_fx");
    btn(GB_SEL); enc(GE_K1, 3); run(0.8); shot("09_kanal_preset");
    enc(GE_PRESETS, 57); run(0.3); shot("10_preset58");
    run(3.0);
    btn(GB_SAVE); run(0.2); shot("11_saved");
    btn(GB_PLAY); run(0.5);

    fseek(wav, 4, SEEK_SET); w32((unsigned)(36 + frames * 4));
    fseek(wav, 40, SEEK_SET); w32((unsigned)(frames * 4));
    fclose(wav);
    printf("Ton: %s (%.1f s), Benutzerstand %d Byte\n", path, (double)frames / SR, app.userLen);
    return 0;
}
