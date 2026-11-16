/* GrooveDeck FM-1 - PC-Werkzeug: rendert ein Werkspreset als WAV und misst die Rechenzeit.
   gd_render [preset-index] [sekunden] [datei.wav]      gd_render --list      gd_render --all (Pruefung aller Presets) */
#include "gd_preset.h"
#include "gd_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define SR 44100
#define BLOCK 128

static gd_engine eng;
static gd_seq seq;

static void wr16(FILE *f, unsigned v) { fputc(v & 255, f); fputc((v >> 8) & 255, f); }
static void wr32(FILE *f, unsigned v) { wr16(f, v & 0xFFFF); wr16(f, v >> 16); }

typedef struct { double peak, rms, cpu; int nan; } stats;

static stats render(int idx, double secs, FILE *wav)
{
    stats st = { 0, 0, 0, 0 };
    float l[BLOCK], r[BLOCK];
    long total = (long)(secs * SR), done = 0;
    double acc = 0.0;
    clock_t c0;
    gd_engine_init(&eng, SR);
    gd_seq_init(&seq, &eng);
    gd_preset_load(&seq, idx);
    gd_seq_set_playing(&seq, 1);
    c0 = clock();
    while (done < total) {
        int n = (int)(total - done < BLOCK ? total - done : BLOCK), i;
        memset(l, 0, sizeof l); memset(r, 0, sizeof r);
        gd_seq_render(&seq, l, r, n);
        for (i = 0; i < n; ++i) {
            if (!(l[i] == l[i]) || !(r[i] == r[i])) { st.nan = 1; l[i] = r[i] = 0; }
            if (fabs(l[i]) > st.peak) st.peak = fabs(l[i]);
            if (fabs(r[i]) > st.peak) st.peak = fabs(r[i]);
            acc += 0.5 * (l[i] * l[i] + r[i] * r[i]);
            if (wav) {
                int a = (int)lrintf(gd_clampf(l[i], -1, 1) * 32767), b = (int)lrintf(gd_clampf(r[i], -1, 1) * 32767);
                wr16(wav, (unsigned)(a & 0xFFFF)); wr16(wav, (unsigned)(b & 0xFFFF));
            }
        }
        done += n;
    }
    st.cpu = (double)(clock() - c0) / CLOCKS_PER_SEC / secs;
    st.rms = sqrt(acc / (double)total);
    return st;
}

int main(int argc, char **argv)
{
    int idx = 0;
    double secs = 8.0;
    const char *out = "preset.wav";
    if (argc > 1 && !strcmp(argv[1], "--list")) {
        int i;
        for (i = 0; i < gd_num_presets; ++i)
            printf("%3d  %-20s %-16s %s\n", i, gd_genre_names[gd_presets[i].genre], gd_sub_names[gd_presets[i].genre][gd_presets[i].sub], gd_presets[i].name);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--all")) {
        int i, bad = 0;
        double worst = 0, sumcpu = 0;
        int wi = 0;
        for (i = 0; i < gd_num_presets; ++i) {
            stats st = render(i, argc > 2 ? atof(argv[2]) : 4.0, NULL);
            sumcpu += st.cpu;
            if (st.cpu > worst) { worst = st.cpu; wi = i; }
            if (st.nan || st.peak > 1.0 || st.rms < 0.005) {
                printf("AUFFAELLIG %3d %-28s peak %.3f rms %.4f nan %d\n", i, gd_presets[i].name, st.peak, st.rms, st.nan);
                ++bad;
            }
        }
        printf("%d Presets geprueft, %d auffaellig. Rechenzeit je Sekunde Audio: Mittel %.2f ms, Maximum %.2f ms (Preset %d)\n",
               gd_num_presets, bad, sumcpu / gd_num_presets * 1000.0, worst * 1000.0, wi);
        return bad ? 1 : 0;
    }
    if (argc > 1) idx = atoi(argv[1]);
    if (argc > 2) secs = atof(argv[2]);
    if (argc > 3) out = argv[3];
    {
        FILE *f = fopen(out, "wb");
        long frames = (long)(secs * SR);
        stats st;
        if (!f) { perror(out); return 1; }
        fwrite("RIFF", 1, 4, f); wr32(f, (unsigned)(36 + frames * 4)); fwrite("WAVEfmt ", 1, 8, f);
        wr32(f, 16); wr16(f, 1); wr16(f, 2); wr32(f, SR); wr32(f, SR * 4); wr16(f, 4); wr16(f, 16);
        fwrite("data", 1, 4, f); wr32(f, (unsigned)(frames * 4));
        st = render(idx, secs, f);
        fclose(f);
        printf("%s: %s / %s / %s  Tempo %.1f  peak %.3f  rms %.4f  Rechenzeit %.1f ms je Sekunde\n", out,
               gd_genre_names[gd_presets[idx].genre], gd_sub_names[gd_presets[idx].genre][gd_presets[idx].sub],
               gd_presets[idx].name, eng.g[GP_TEMPO], st.peak, st.rms, st.cpu * 1000.0);
    }
    return 0;
}
