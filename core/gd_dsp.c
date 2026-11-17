/* GrooveDeck FM-1 - Klangerzeugung, Portierung von GrooveDeck Source/Dsp.h (gdsp::Engine) nach C99. */
#include "gd_dsp.h"
#include "gd_math.h"
#include <string.h>

/* ------------------------------------------------------------------ Filter */
static void svf_set(gd_svf *f, float fc, float res, float sr)
{
    float g;
    fc = gd_clampf(fc, 20.0f, sr * 0.45f);
    res = gd_clampf(res, 0.0f, 1.0f);
    g = gd_tan(GD_PI * fc / sr);
    f->k = 2.0f - 1.96f * res;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}
static inline float svf_process(gd_svf *f, float x, int type)
{
    const float v3 = x - f->ic2;
    const float v1 = f->a1 * f->ic1 + f->a2 * v3;
    const float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    switch (type) {
        case 0: return v2;
        case 1: return x - f->k * v1 - v2;
        case 2: return f->k * v1;
        default: return v1;
    }
}
static void svf_copy(gd_svf *d, const gd_svf *s) { d->a1 = s->a1; d->a2 = s->a2; d->a3 = s->a3; d->k = s->k; }
static void svf_reset(gd_svf *f) { f->ic1 = f->ic2 = 0.0f; }

static float ladder_critical(int model, float fw)
{
    static const float t3[19] = { 8.034f, 8.046f, 8.063f, 8.086f, 8.118f, 8.161f, 8.221f, 8.303f, 8.416f, 8.574f, 8.795f,
                                  9.109f, 9.561f, 10.223f, 11.224f, 12.796f, 15.408f, 20.101f, 22.714f };
    static const float t4[19] = { 4.011f, 4.015f, 4.021f, 4.029f, 4.039f, 4.054f, 4.073f, 4.100f, 4.138f, 4.189f, 4.261f,
                                  4.362f, 4.505f, 4.710f, 5.013f, 5.471f, 6.192f, 7.393f, 9.559f };
    const float pos = gd_clampf(gd_log2(fw / 0.00045f) / 8.47396f * 19.0f, 0.0f, 18.0f); /* log2(0.16/0.00045) */
    const int i = gd_clampi((int)pos, 0, 17);
    const float f = pos - (float)i;
    const float *t = model == 1 ? t3 : t4;
    return t[i] * (1.0f - f) + t[i + 1] * f;
}
static void ladder_set(gd_ladder *l, float fc, float res, float sr, int model)
{
    float fw;
    res = gd_clampf(res, 0.0f, 1.0f);
    fw = gd_clampf(fc, 20.0f, sr * 0.45f) / sr;
    l->open = gd_clampf((fw - 0.1f) / 0.25f, 0.0f, 1.0f);
    fw = gd_minf(fw, 0.115f);
    l->g = 1.0f - gd_exp(-GD_TWO_PI * fw);
    l->k = res * 1.04f * ladder_critical(model, fw);
    l->comp = 1.0f + 0.35f * l->k / (model == 1 ? 2.0f : 1.0f);
}
static inline float ladder_process(gd_ladder *l, float x, int model)
{
    const float fb = model == 1 ? l->s[2] : l->s[3];
    const float in = gd_tanh(x - l->k * fb);
    float y;
    l->s[0] += l->g * (in - l->s[0]);
    l->s[1] += l->g * (l->s[0] - l->s[1]);
    l->s[2] += l->g * (l->s[1] - l->s[2]);
    if (model != 1) l->s[3] += l->g * (l->s[2] - l->s[3]);
    y = (model == 1 ? l->s[2] : l->s[3]) * l->comp;
    if (!(gd_absf(y) < 8.0f)) { l->s[0] = l->s[1] = l->s[2] = l->s[3] = 0.0f; return 0.0f; }
    return y + l->open * (x - y);
}

static inline float poly_blep(float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
static inline float tri01(float ph) { return ph < 0.25f ? 4.0f * ph : (ph < 0.75f ? 2.0f - 4.0f * ph : 4.0f * ph - 4.0f); }
static inline float ea_osc(int w, float ph, float dt)
{
    dt = gd_minf(dt, 0.4f);
    if (w == 0) return 2.0f * ph - 1.0f - poly_blep(ph, dt);
    if (w == 1) {
        float p2 = ph + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        return (ph < 0.5f ? 1.0f : -1.0f) + poly_blep(ph, dt) - poly_blep(p2, dt);
    }
    return tri01(ph);
}

/* ------------------------------------------------------------- Synth-Defs */
enum { S_DRUM, S_CLAP, S_COWBELL, S_PAD, S_GLITCH, S_NOISE, S_SYNTHER, S_METAL };
typedef struct { int kind; float f0, pitchOct, pitchTau, tone, noise, hp, decay; } synth_def;
static const synth_def k_defs[13] = {
    { S_SYNTHER, 0, 0, 0, 0, 0, 0, 0.3f }, { S_SYNTHER, 0, 0, 0, 0, 0, 0, 0.3f },
    { S_SYNTHER, 0, 0, 0, 0, 0, 0, 0.3f }, { S_SYNTHER, 0, 0, 0, 0, 0, 0, 0.3f },
    { S_CLAP, 0, 0, 0, 0, 1.0f, 900.0f, 0.28f },
    { S_METAL, 205.3f, 0, 0, 1.0f, 0.5f, 6500.0f, 0.2f }, { S_METAL, 205.3f, 0, 0, 1.0f, 0.5f, 6500.0f, 0.4f },
    { S_METAL, 140.0f, 0, 0, 1.0f, 0.8f, 4500.0f, 1.0f }, { S_METAL, 170.0f, 0, 0, 1.0f, 0.7f, 5000.0f, 1.0f },
    { S_PAD, 130.81f, 0, 0, 1.0f, 0, 0, 1.2f }, { S_PAD, 261.63f, 0, 0, 1.0f, 0, 0, 1.0f },
    { S_GLITCH, 0, 0, 0, 0, 1.0f, 300.0f, 0.3f }, { S_NOISE, 0, 0, 0, 0, 1.0f, 0, 0.5f }
};
static const synth_def *sdef(int part) { return &k_defs[gd_clampi(part, 0, 12)]; }

/* --------------------------------------------------------------- Defaults */
typedef struct { float pitch, modType, modDepth, modSpeed, egTime; } er_default;
static const er_default k_er[9] = {
    { 0.158f, 4, 0.78f, 0.35f, 0.50f }, { 0.352f, 0, 0.60f, 0.50f, 0.38f }, { 0.266f, 4, 0.66f, 0.45f, 0.45f },
    { 0.595f, 0, 0.50f, 0.50f, 0.18f }, { 0.5f, 0, 0.5f, 0, 0.40f }, { 0.5f, 0, 0.5f, 0, 0.12f },
    { 0.5f, 0, 0.5f, 0, 0.45f }, { 0.5f, 0, 0.5f, 0, 0.80f }, { 0.5f, 0, 0.5f, 0, 0.85f }
};
static const float k_extra_def[26] = { 0, 0, 0, 0, 0, 0.2015f, 0, 0.5f, 0, 2.0f, 0.5f, 0.35f, 0.5f,
                                       0, 0, 0, 0.5f, 0, 1.0f, 0, 0, 0, 0, 1.0f, 0, 1.0f };
static const float k_arp_def[20] = { 0, 0, 2, 3, 1, 0.5f, 0, 0.5f, 0, 3, 0, 7, 0, 3, 7, 10, 12, 10, 7, 3 };

float gd_part_default(int part, int f)
{
    if (f >= PF_A0) return k_arp_def[f - PF_A0];
    if (f == PF_SRC) return part <= 8 ? ((part == 1 || part == 3) ? 2.0f : 1.0f) : 0.0f;
    if (f == PF_TUBE) return 0.0f;
    if (f >= PF_X0) {
        if (f == PF_MODNOISE && part == 1) return 1.0f;
        return k_extra_def[f - PF_X0];
    }
    if (part >= 0 && part <= 8) {
        const er_default *d = &k_er[part];
        if (f == PF_PITCH) return d->pitch;
        if (f == PF_MODTYPE) return d->modType;
        if (f == PF_MODDEPTH) return d->modDepth;
        if (f == PF_MODSPEED) return d->modSpeed;
        if (f == PF_EGTIME) return d->egTime;
    }
    if (f == PF_LEVEL) return 0.8f;
    if (f == PF_PAN) return 0.5f;
    if (f == PF_CUTOFF) return 1.0f;
    if (f == PF_EGINT || f == PF_MODDEPTH) return 0.5f;
    if (f == PF_PITCH && !gd_is_kbd(part)) return 0.5f;
    return 0.0f;
}

float gd_global_default(int id)
{
    switch (id) {
        case GP_TEMPO: return 120.0f;
        case GP_GATE: return 0.5f;
        case GP_GOODON: return 1.0f;
        case GP_GOODAMT: return 0.3f;
        case GP_GOODAUTO: return 1.0f;
        case GP_MASTERVOL: return 0.8f;
        default: return 0.0f;
    }
}

float gd_master_gain(float v)
{
    float db;
    v = gd_clampf(v, 0.0f, 1.0f);
    if (v < 0.004f) return 0.0f;
    db = (v - 0.8f) * (v < 0.8f ? 60.0f : 30.0f);
    return gd_db2lin(db);
}

/* ---------------------------------------------------------- FX-Speicher */
static int16_t g_delay[2][GD_DELAY_LEN];
static float g_rev[GD_REVERB_LEN];
static int g_delayOwner = -1, g_revOwner = -1;
static int g_combOff[2][8], g_combLen[2][8], g_apOff[2][4], g_apLen[2][4];

static inline float s16f(int16_t v) { return (float)v * (1.0f / 16384.0f); }
static inline int16_t f16s(float x)
{
    x *= 16384.0f;
    if (x > 32767.0f) x = 32767.0f;
    if (x < -32768.0f) x = -32768.0f;
    return (int16_t)(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

static void reverb_layout(float sr)
{
    static const int ct[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static const int at[4] = { 556, 441, 341, 225 };
    const int combCh = GD_REVERB_STEREO ? 2 : 1;
    float sc = sr / 44100.0f, need = 0.0f;
    int ch, i, off = 0;
    for (ch = 0; ch < combCh; ++ch) for (i = 0; i < 8; ++i) need += (float)(ct[i] + ch * 23) + 1.0f;
    for (ch = 0; ch < 2; ++ch) for (i = 0; i < 4; ++i) need += (float)(at[i] + ch * 23) + 1.0f;
    if (need * sc > (float)GD_REVERB_LEN) sc = (float)GD_REVERB_LEN / need;
    for (ch = 0; ch < 2; ++ch)
        for (i = 0; i < 8; ++i) {
            if (ch < combCh) { g_combLen[ch][i] = (int)((float)(ct[i] + ch * 23) * sc) + 1; g_combOff[ch][i] = off; off += g_combLen[ch][i]; }
            else { g_combLen[ch][i] = g_combLen[0][i]; g_combOff[ch][i] = g_combOff[0][i]; }
        }
    for (ch = 0; ch < 2; ++ch)
        for (i = 0; i < 4; ++i) { g_apLen[ch][i] = (int)((float)(at[i] + ch * 23) * sc) + 1; g_apOff[ch][i] = off; off += g_apLen[ch][i]; }
}

static int fx_needs_delay(int t) { return t == 1 || t == 2 || t == 3 || t == 4 || t == 5 || t == 9; }

static void fx_clear(gd_fx *f)
{
    int i, j;
    if (f->delayRes) memset(g_delay, 0, sizeof g_delay);
    if (f->revRes) memset(g_rev, 0, sizeof g_rev);
    for (i = 0; i < 2; ++i) {
        for (j = 0; j < 8; ++j) { f->ci[i][j] = 0; f->clp[i][j] = 0.0f; }
        for (j = 0; j < 4; ++j) { f->ai[i][j] = 0; f->phZ[i][j] = 0.0f; }
        f->phFb[i] = 0.0f;
        f->bqZ[i][0] = f->bqZ[i][1] = 0.0f;
        svf_reset(&f->svfA[i]); svf_reset(&f->svfB[i]); svf_reset(&f->svfF[i]);
    }
    f->eqE1 = f->eqE2 = -1.0f;
    f->env = 0.0f; f->decCnt = 0; f->decL = f->decR = 0.0f;
    f->lfo = f->grainPh = f->psPh = 0.0f;
    f->wp = 0;
}

static void fx_set(gd_fx *f, int idx, int t, float a, float b)
{
    f->e1 = gd_clampf(a, 0.0f, 1.0f);
    f->e2 = gd_clampf(b, 0.0f, 1.0f);
    t = gd_clampi(t, 0, 15);
    if (t == f->type) return;
    if (g_delayOwner == idx) g_delayOwner = -1;
    if (g_revOwner == idx) g_revOwner = -1;
    f->type = t;
    f->delayRes = 0;
    f->revRes = 0;
    if (fx_needs_delay(t) && g_delayOwner < 0) { g_delayOwner = idx; f->delayRes = 1; }
    if (t == 0 && g_revOwner < 0) { g_revOwner = idx; f->revRes = 1; }
    fx_clear(f);
}

static inline float fx_rd(const gd_fx *f, int ch, float d)
{
    const int N = GD_DELAY_LEN;
    float p, fr;
    int i0, i1;
    d = gd_clampf(d, 1.0f, (float)(N - 2));
    p = (float)f->wp - d;
    if (p < 0.0f) p += (float)N;
    i0 = (int)p;
    if (i0 > N - 1) i0 = N - 1;
    fr = p - (float)i0;
    i1 = i0 + 1 >= N ? 0 : i0 + 1;
    return s16f(g_delay[ch][i0]) * (1.0f - fr) + s16f(g_delay[ch][i1]) * fr;
}

static void fx_process(gd_fx *f, float sr, float bpm, float inL, float inR, float *oL, float *oR)
{
    const float in[2] = { inL, inR };
    float out[2];
    int ch, s;
    out[0] = inL; out[1] = inR;
    if ((fx_needs_delay(f->type) && !f->delayRes) || (f->type == 0 && !f->revRes)) { *oL = inL; *oR = inR; return; }
    switch (f->type) {
        case 0: { /* REVERB (Freeverb-Variante wie im Plugin; GD_REVERB_STEREO 0: Kammfilter mono) */
            const float fb = 0.70f + 0.28f * f->e1, damp = 0.3f;
            const float mono = (inL + inR) * 0.015f;
            float acc0 = 0.0f;
            int i;
            for (ch = 0; ch < 2; ++ch) {
                float acc = 0.0f;
                if (GD_REVERB_STEREO || ch == 0) {
                    for (i = 0; i < 8; ++i) {
                        float *b = &g_rev[g_combOff[ch][i]];
                        const float y = b[f->ci[ch][i]];
                        f->clp[ch][i] = y * (1.0f - damp) + f->clp[ch][i] * damp;
                        b[f->ci[ch][i]] = mono + f->clp[ch][i] * fb;
                        if (++f->ci[ch][i] >= g_combLen[ch][i]) f->ci[ch][i] = 0;
                        acc += y;
                    }
                    acc0 = acc;
                } else acc = acc0;
                for (i = 0; i < 4; ++i) {
                    float *b = &g_rev[g_apOff[ch][i]];
                    const float bo = b[f->ai[ch][i]];
                    const float o = -acc + bo;
                    b[f->ai[ch][i]] = acc + bo * 0.5f;
                    if (++f->ai[ch][i] >= g_apLen[ch][i]) f->ai[ch][i] = 0;
                    acc = o;
                }
                out[ch] = in[ch] * (1.0f - 0.8f * f->e2) + acc * 3.0f * f->e2;
            }
            break;
        }
        case 1:
        case 2: { /* BPM SYNC DELAY / SHORT DELAY */
            static const float frac[7] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };
            float d, fb, wl, wr;
            if (f->type == 1) d = 60.0f / bpm * frac[gd_clampi((int)(f->e1 * 6.999f), 0, 6)] * sr;
            else d = 0.001f * gd_powf(300.0f, f->e1) * sr;
            d = gd_clampf(d, 2.0f, (float)(GD_DELAY_LEN - 4));
            fb = f->e2 * 0.9f;
            wl = fx_rd(f, 0, d); wr = fx_rd(f, 1, d);
            g_delay[0][f->wp] = f16s(in[0] + fb * (f->type == 1 ? wr : wl));
            g_delay[1][f->wp] = f16s(in[1] + fb * (f->type == 1 ? wl : wr));
            out[0] = in[0] * 0.7f + wl * 0.7f;
            out[1] = in[1] * 0.7f + wr * 0.7f;
            break;
        }
        case 3:
        case 5: { /* MOD DELAY / CHO-FLG */
            const float rate = f->type == 3 ? 0.4f : 0.1f * gd_powf(50.0f, f->e1);
            const float base = f->type == 3 ? (0.005f + 0.035f * f->e1) : 0.012f;
            const float dep = f->type == 3 ? 0.004f * (0.2f + f->e2) : 0.006f * f->e2;
            const float fb = f->type == 3 ? 0.4f : 0.25f * f->e2;
            float w[2];
            f->lfo += rate / sr; f->lfo -= gd_floorf(f->lfo);
            for (ch = 0; ch < 2; ++ch) {
                const float sn = gd_sin01(f->lfo + (ch == 1 ? 0.5f : 0.0f));
                w[ch] = fx_rd(f, ch, gd_clampf((base + dep * sn) * sr, 2.0f, (float)(GD_DELAY_LEN - 4)));
            }
            g_delay[0][f->wp] = f16s(in[0] + fb * w[0]);
            g_delay[1][f->wp] = f16s(in[1] + fb * w[1]);
            out[0] = in[0] * 0.6f + w[0] * 0.6f;
            out[1] = in[1] * 0.6f + w[1] * 0.6f;
            break;
        }
        case 4: { /* GRAIN SHIFTER */
            const float len = gd_clampf((0.01f + 0.11f * f->e1) * sr, 64.0f, (float)(GD_DELAY_LEN - 4) * 0.5f);
            float w[2] = { 0.0f, 0.0f }, before;
            int h;
            g_delay[0][f->wp] = f16s(in[0]);
            g_delay[1][f->wp] = f16s(in[1]);
            for (h = 0; h < 2; ++h) {
                const float ph = f->grainPh + (h == 1 ? 0.5f : 0.0f);
                const float pp = ph - gd_floorf(ph);
                const float win = 1.0f - gd_absf(2.0f * pp - 1.0f);
                for (ch = 0; ch < 2; ++ch) w[ch] += fx_rd(f, ch, 4.0f + f->grainOff[h] * len * 0.5f + pp * len) * win;
            }
            before = f->grainPh;
            f->grainPh += 1.0f / len;
            for (h = 0; h < 2; ++h) {
                const float a = before + (h == 1 ? 0.5f : 0.0f), b = f->grainPh + (h == 1 ? 0.5f : 0.0f);
                if (gd_floorf(b) > gd_floorf(a)) f->grainOff[h] = 0.5f * (gd_rnd(&f->rng) + 1.0f);
            }
            if (f->grainPh >= 2.0f) f->grainPh -= 2.0f;
            out[0] = in[0] * (1.0f - 0.7f * f->e2) + w[0] * f->e2 * 1.2f;
            out[1] = in[1] * (1.0f - 0.7f * f->e2) + w[1] * f->e2 * 1.2f;
            break;
        }
        case 6: { /* PHASER */
            float fc, t, a, fb;
            f->lfo += 0.1f * gd_powf(60.0f, f->e1) / sr; f->lfo -= gd_floorf(f->lfo);
            fc = 300.0f * gd_exp2(3.32192809f * 0.5f * (1.0f + gd_sin01(f->lfo)));
            t = gd_tan(GD_PI * gd_minf(fc, sr * 0.45f) / sr);
            a = (t - 1.0f) / (t + 1.0f);
            fb = 0.85f * f->e2;
            for (ch = 0; ch < 2; ++ch) {
                float x = in[ch] + fb * f->phFb[ch];
                for (s = 0; s < 4; ++s) {
                    const float y = a * x + f->phZ[ch][s];
                    f->phZ[ch][s] = x - a * y;
                    x = y;
                }
                f->phFb[ch] = x;
                out[ch] = 0.5f * (in[ch] + x);
            }
            break;
        }
        case 7: { /* RING MOD */
            float c;
            f->lfo += 20.0f * gd_powf(100.0f, f->e1) / sr; f->lfo -= gd_floorf(f->lfo);
            c = gd_sin01(f->lfo);
            for (ch = 0; ch < 2; ++ch) out[ch] = in[ch] * (1.0f - f->e2) + in[ch] * c * f->e2;
            break;
        }
        case 8: { /* TALKING MOD */
            float m;
            f->lfo += 0.2f * gd_powf(25.0f, f->e1) / sr; f->lfo -= gd_floorf(f->lfo);
            m = 0.5f + 0.5f * gd_sin01(f->lfo);
            if (++f->fcnt >= 8) {
                f->fcnt = 0;
                svf_set(&f->formA, 700.0f + (300.0f - 700.0f) * m, 0.85f, sr);
                svf_set(&f->formB, 1200.0f + (2300.0f - 1200.0f) * m, 0.85f, sr);
                for (ch = 0; ch < 2; ++ch) { svf_copy(&f->svfA[ch], &f->formA); svf_copy(&f->svfB[ch], &f->formB); }
            }
            for (ch = 0; ch < 2; ++ch) {
                const float wet = svf_process(&f->svfA[ch], in[ch], 2) + svf_process(&f->svfB[ch], in[ch], 2);
                out[ch] = in[ch] * (1.0f - 0.8f * f->e2) + wet * 1.5f * f->e2;
            }
            break;
        }
        case 9: { /* PITCH SHIFTER */
            const float win = 0.05f * sr;
            const float ratio = gd_exp2((f->e1 - 0.5f) * 2.0f);
            float w[2] = { 0.0f, 0.0f };
            int h;
            g_delay[0][f->wp] = f16s(in[0]);
            g_delay[1][f->wp] = f16s(in[1]);
            for (h = 0; h < 2; ++h) {
                float p = f->psPh + (h == 1 ? 0.5f : 0.0f), g;
                p -= gd_floorf(p);
                g = 1.0f - gd_absf(2.0f * p - 1.0f);
                for (ch = 0; ch < 2; ++ch) w[ch] += fx_rd(f, ch, 2.0f + p * win) * g;
            }
            f->psPh += (1.0f - ratio) / win;
            f->psPh -= gd_floorf(f->psPh);
            for (ch = 0; ch < 2; ++ch) out[ch] = in[ch] * (1.0f - f->e2) + w[ch] * f->e2;
            break;
        }
        case 10: { /* COMPRESSOR */
            const float level = gd_maxf(gd_absf(in[0]), gd_absf(in[1]));
            const float aC = gd_exp(-1.0f / (0.005f * sr)), rC = gd_exp(-1.0f / (0.12f * sr));
            const float thr = gd_db2lin(-6.0f - 34.0f * f->e1);
            float g = 1.0f, mk;
            f->env = level > f->env ? aC * f->env + (1.0f - aC) * level : rC * f->env + (1.0f - rC) * level;
            if (f->env > thr) g = gd_powf(f->env / thr, 0.25f - 1.0f);
            mk = gd_db2lin(f->e2 * 9.0f);
            out[0] = in[0] * g * mk;
            out[1] = in[1] * g * mk;
            break;
        }
        case 11: { /* DISTORTION */
            const float g = 1.0f + 40.0f * f->e1 * f->e1;
            const float lv = (0.3f + 0.9f * f->e2) / gd_sqrt(gd_sqrt(g));
            for (ch = 0; ch < 2; ++ch) out[ch] = gd_tanh(g * in[ch]) * lv;
            break;
        }
        case 12: { /* DECIMATOR */
            const int hold = 1 + (int)(f->e1 * f->e1 * 60.0f);
            if (f->decCnt <= 0) {
                const float lev = gd_exp2(16.0f - f->e2 * 12.0f) * 0.5f;
                f->decCnt = hold;
                f->decL = (float)gd_roundi(in[0] * lev) / lev;
                f->decR = (float)gd_roundi(in[1] * lev) / lev;
            }
            --f->decCnt;
            out[0] = f->decL;
            out[1] = f->decR;
            break;
        }
        case 13: { /* EQ */
            if (f->e1 != f->eqE1 || f->e2 != f->eqE2) {
                float f0, A, w0, alpha, a0, cw;
                f->eqE1 = f->e1; f->eqE2 = f->e2;
                f0 = 200.0f * gd_powf(40.0f, f->e1);
                A = gd_powf(10.0f, (f->e2 - 0.5f) * 2.0f * 15.0f / 40.0f);
                w0 = GD_TWO_PI * gd_minf(f0, sr * 0.45f) / sr;
                alpha = gd_sin01(w0 / GD_TWO_PI) / 2.0f;
                cw = gd_cos01(w0 / GD_TWO_PI);
                a0 = 1.0f + alpha / A;
                f->bq[0] = (1.0f + alpha * A) / a0;
                f->bq[1] = (-2.0f * cw) / a0;
                f->bq[2] = (1.0f - alpha * A) / a0;
                f->bq[3] = (-2.0f * cw) / a0;
                f->bq[4] = (1.0f - alpha / A) / a0;
            }
            for (ch = 0; ch < 2; ++ch) {
                const float y = f->bq[0] * in[ch] + f->bqZ[ch][0];
                f->bqZ[ch][0] = f->bq[1] * in[ch] - f->bq[3] * y + f->bqZ[ch][1];
                f->bqZ[ch][1] = f->bq[2] * in[ch] - f->bq[4] * y;
                out[ch] = y;
            }
            break;
        }
        case 14:
        case 15: { /* LPF / HPF */
            if (++f->fcnt >= 8) {
                f->fcnt = 0;
                svf_set(&f->filt, 20.0f * gd_powf(1000.0f, f->type == 14 ? f->e1 : f->e1 * 0.9f), f->e2, sr);
            }
            for (ch = 0; ch < 2; ++ch) {
                svf_copy(&f->svfF[ch], &f->filt);
                out[ch] = svf_process(&f->svfF[ch], in[ch], f->type == 14 ? 0 : 1);
            }
            break;
        }
        default: break;
    }
    f->wp = (f->wp + 1 >= GD_DELAY_LEN) ? 0 : f->wp + 1;
    *oL = out[0];
    *oR = out[1];
}

/* ------------------------------------------------------- Soundgoodizer */
static void good_prepare(gd_good *g, float sr)
{
    static const float fc[12] = { 40, 63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 8000 };
    int i;
    memset(g, 0, sizeof *g);
#if GD_GOOD_FULLRATE
    for (i = 0; i < 12; ++i) svf_set(&g->bp[i], fc[i], 0.35f, sr);
    g->eC = 1.0f - gd_exp(-1.0f / (0.25f * sr));
#else
    for (i = 0; i < 12; ++i) svf_set(&g->bp[i], fc[i], 0.35f, sr * 0.5f); /* Analyse mit halber Rate */
    g->eC = 1.0f - gd_exp(-1.0f / (0.25f * sr * 0.5f));
#endif
    g->aLow = 1.0f - gd_exp(-GD_TWO_PI * 200.0f / sr);
    g->aHigh = 1.0f - gd_exp(-GD_TWO_PI * 3500.0f / sr);
    g->gr = 1.0f; g->ga = 1.0f;
    g->pC = 1.0f - gd_exp(-1.0f / (0.4f * sr));
    g->kLo = g->kHi = 0.0f;
}
static void good_set(gd_good *g, int on, float amt, int style, int autoLvl)
{
    float ratio, d;
    g->on = (uint8_t)on;
    g->autoLvl = (uint8_t)autoLvl;
    g->amt = gd_clampf(amt, 0.0f, 1.0f);
    g->style = gd_clampi(style, 0, 4);
    ratio = 1.0f + 3.0f * g->amt;
    g->makeup = gd_sqrt(1.0f / (0.35f + 0.65f / ratio)) * 0.92f;
    d = 1.0f + 2.0f * g->amt * (g->style == 2 ? 1.6f : 1.0f);
    g->satD = d;
    g->satK = (1.0f + 0.35f * (d - 1.0f)) / d;
}
static void good_process(gd_good *g, float *l, float *r)
{
    const float m = 0.5f * (*l + *r);
    float inP, peak, att, rel, gg, ratio;
    int i, c;
#if GD_GOOD_FULLRATE
    for (i = 0; i < 12; ++i) { const float b = svf_process(&g->bp[i], m, 2); g->e[i] += g->eC * (b * b - g->e[i]); }
    if (0) {
#else
    g->half = g->half ? 0u : 1u;   /* kein ^=: Speicher-XOR kennt der Emulator nicht */
    if (g->half != 0u) g->prevM = m;
    else {
#endif
        const float x = 0.5f * (m + g->prevM);
        for (i = 0; i < 12; ++i) { const float b = svf_process(&g->bp[i], x, 2); g->e[i] += g->eC * (b * b - g->e[i]); }
    }
    if (++g->cnt >= 512) {
        float lo = 0, mi = 0, hi = 0, tLo = 0, tHi = 0;
        g->cnt = 0;
        for (i = 0; i < 12; ++i) {
            const float db = 3.01029996f * gd_log2(g->e[i] + 1e-12f);
            g->bands[i] = gd_clampf((db + 70.0f) / 50.0f, 0.0f, 1.0f);
        }
        for (i = 0; i < 4; ++i) lo += g->e[i];
        for (i = 4; i < 9; ++i) mi += g->e[i];
        for (i = 9; i < 12; ++i) hi += g->e[i];
        if (mi > 1e-7f) {
            static const float TL[5] = { 1, 3, 3, 0, 1 }, TH[5] = { -4, -4, -7, 0, -3 };
            const float dL = 3.01029996f * gd_log2((lo + 1e-9f) / (mi + 1e-9f));
            const float dH = 3.01029996f * gd_log2((hi + 1e-9f) / (mi + 1e-9f));
            tLo = gd_clampf(TL[g->style] - dL, -6.0f, 6.0f) * g->amt;
            tHi = gd_clampf(TH[g->style] - dH, -6.0f, 6.0f) * g->amt;
        }
        g->gLo += 0.05f * (tLo - g->gLo);
        g->gHi += 0.05f * (tHi - g->gHi);
        g->kLo = gd_db2lin(g->gLo) - 1.0f;
        g->kHi = gd_db2lin(g->gHi) - 1.0f;
    }
    if (!g->on || g->amt < 0.001f) { g->reduction = 0.0f; g->levelDb = 0.0f; g->ga = 1.0f; return; }
    inP = 0.5f * (*l * *l + *r * *r);
    {
        float *x[2];
        x[0] = l; x[1] = r;
        for (c = 0; c < 2; ++c) {
            float v = *x[c];
            g->lpL[c] += g->aLow * (v - g->lpL[c]);
            g->lpH[c] += g->aHigh * (v - g->lpH[c]);
            v += g->kLo * g->lpL[c] + g->kHi * (v - g->lpH[c]);
            *x[c] = v;
        }
    }
    peak = gd_maxf(gd_absf(*l), gd_absf(*r));
    att = g->style == 1 ? 0.0015f : 0.008f;
    rel = g->style == 1 ? 0.0003f : 0.0006f;
    g->env += (peak > g->env ? att : rel) * (peak - g->env);
    ratio = 1.0f + 3.0f * g->amt;
    gg = 1.0f;
    if (g->env > 0.35f) gg = (0.35f + (g->env - 0.35f) / ratio) / g->env;
    g->gr += 0.02f * (gg - g->gr);
    *l *= g->gr * g->makeup;
    *r *= g->gr * g->makeup;
    *l = gd_tanh(g->satD * *l) * g->satK;
    *r = gd_tanh(g->satD * *r) * g->satK;
    if (g->style == 4) {
        const float mm = 0.5f * (*l + *r), ss = 0.5f * (*l - *r) * (1.0f + 0.6f * g->amt);
        *l = mm + ss;
        *r = mm - ss;
    }
    g->pIn += g->pC * (inP - g->pIn);
    g->pOut += g->pC * (0.5f * (*l * *l + *r * *r) - g->pOut);
    if (g->autoLvl) {
        const float tgt = g->pOut > 1e-9f ? gd_sqrt(gd_maxf(g->pIn, 1e-12f) / g->pOut) : 1.0f;
        g->ga += 0.0004f * (gd_clampf(tgt, 0.25f, 4.0f) - g->ga);
    } else g->ga += 0.002f * (1.0f - g->ga);
    *l *= g->ga;
    *r *= g->ga;
    *l = 0.95f * gd_tanh(*l / 0.95f);
    *r = 0.95f * gd_tanh(*r / 0.95f);
    if (++g->lvCnt >= 2048) {
        g->lvCnt = 0;
        g->reduction = -gd_lin2db(gd_maxf(g->gr, 0.01f));
        g->levelDb = g->pIn > 1e-9f ? 3.01029996f * gd_log2(gd_maxf(g->pOut * g->ga * g->ga, 1e-12f) / g->pIn) : 0.0f;
    }
}

/* -------------------------------------------------------------- Engine */
void gd_engine_reset_patch(gd_engine *e)
{
    int p, f;
    const float mv = e->g[GP_MASTERVOL];
    for (p = 0; p < GD_PARTS; ++p)
        for (f = 0; f < PF_COUNT; ++f) e->parts[p].p[f] = gd_part_default(p, f);
    for (f = 0; f < GP_COUNT; ++f) e->g[f] = gd_global_default(f);
    e->g[GP_MASTERVOL] = mv;
}

void gd_engine_init(gd_engine *e, float sampleRate)
{
    int p, i;
    memset(e, 0, sizeof *e);
    e->sr = sampleRate > 1.0f ? sampleRate : 44100.0f;
    e->bpm = 120.0f;
    e->mGain = 1.0f;
    e->relCoef = gd_powf(0.9993f, 44100.0f / e->sr);
    e->g[GP_MASTERVOL] = 0.8f;
    gd_engine_reset_patch(e);
    for (p = 0; p < GD_PARTS; ++p) { e->parts[p].rng = 12345u; e->parts[p].lastNote = 60.0f; }
    g_delayOwner = g_revOwner = -1;
    reverb_layout(e->sr);
    memset(g_delay, 0, sizeof g_delay);
    memset(g_rev, 0, sizeof g_rev);
    for (i = 0; i < 3; ++i) {
        e->fx[i].type = -1;
        e->fx[i].rng = 777u + (uint32_t)i;
        e->fx[i].grainOff[1] = 0.5f;
    }
    good_prepare(&e->good, e->sr);
    gd_engine_update(e);
}

static void part_update(gd_engine *e, int part)
{
    gd_part *P = &e->parts[part];
    const float *q = P->p;
    const float sr = e->sr;
    float decayT, rel, ampT, pmT, menvT, glideT, lb;
    static const float swT[3] = { 0.12f, 0.35f, 1.0f };
    int mf;

    lb = gd_clampf(q[PF_LOWBOOST], 0.0f, 1.0f);
    P->lbA = 1.0f - gd_exp(-GD_TWO_PI * 150.0f * (1.0f - 0.65f * lb) / sr);
    P->lbG = gd_exp2(3.32192809f * lb) - 1.0f;
    decayT = 0.02f * gd_powf(200.0f, gd_clampf(q[PF_EGTIME], 0.0f, 1.0f));
    P->decayCoef = gd_exp(-6.9f / (decayT * sr));
    rel = (gd_clampf(q[PF_ACCDECAY], 0.0f, 1.0f) - 0.5f) * 2.0f;
    P->accCoef = gd_exp(-6.9f / (decayT * gd_exp2(2.0f * rel) * sr));
    P->sweepCoef = gd_exp(-6.9f / (swT[gd_clampi(gd_roundi(q[PF_SWEEPSPD]), 0, 2)] * sr));
    mf = gd_clampi(gd_roundi(q[PF_MUFFLER]), 0, 2);
    P->mufA = 1.0f - gd_exp(-GD_TWO_PI * (mf == 1 ? 5000.0f : 2200.0f) / sr);
    ampT = 0.02f * gd_powf(200.0f, gd_clampf(q[PF_AMPDECAY], 0.0f, 1.0f));
    P->ampCoef = gd_exp(-6.9f / (ampT * sr));
    pmT = 0.005f * gd_powf(400.0f, gd_clampf(q[PF_PMSPEED], 0.0f, 1.0f));
    P->pmCoef = gd_exp(-6.9f / (pmT * sr));
    menvT = 0.005f * gd_powf(400.0f, gd_clampf(q[PF_MODSPEED], 0.0f, 1.0f));
    P->menvCoef = gd_exp(-6.9f / (menvT * sr));
    if (part < 13) P->natCoef = gd_exp(-6.9f / (sdef(part)->decay * sr));
    glideT = gd_is_kbd(part) ? q[PF_PITCH] * 0.5f : 0.0f;
    P->glideCoef = glideT > 0.001f ? 1.0f - gd_exp(-1.0f / (glideT * sr)) : 1.0f;

    /* Raten der beiden LFOs (freilaufend) */
    if (q[PF_MODSYNC] > 0.5f) {
        static const float mul[6] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };
        P->lfoRate = e->bpm / 60.0f * mul[gd_clampi((int)(q[PF_MODSPEED] * 5.999f), 0, 5)];
    } else if (part <= 8) P->lfoRate = 0.1f * gd_powf(50000.0f, q[PF_MODSPEED]);
    else P->lfoRate = 0.05f * gd_powf(600.0f, q[PF_MODSPEED]);
    P->plfoRate = part <= 8 ? 0.1f * gd_powf(50000.0f, q[PF_PMSPEED]) : 0.05f * gd_powf(600.0f, q[PF_PMSPEED]);
    P->cutBase = 20.0f * gd_powf(1000.0f, gd_clampf(q[PF_CUTOFF], 0.0f, 1.0f));
    P->erBase = 20.0f * gd_powf(600.0f, gd_clampf(q[PF_PITCH], 0.0f, 1.0f));
    P->osc2Ratio = gd_exp2(-1.0f + 5.0f * q[PF_OSC2P]);
    P->pitchRatio = gd_exp2((q[PF_PITCH] - 0.5f) * 4.0f); /* (pitch-0.5)*48 Halbtoene */
}

void gd_engine_update(gd_engine *e)
{
    int p;
    e->bpm = gd_maxf(1.0f, e->g[GP_TEMPO]);
    good_set(&e->good, e->g[GP_GOODON] > 0.5f, e->g[GP_GOODAMT], gd_roundi(e->g[GP_GOODSTYLE]), e->g[GP_GOODAUTO] > 0.5f);
    for (p = 0; p < GD_PARTS; ++p) if (p != GD_ACCENT) part_update(e, p);
    for (p = 0; p < 3; ++p)
        fx_set(&e->fx[p], p, gd_roundi(e->g[GP_FX1_TYPE + 3 * p]), e->g[GP_FX1_E1 + 3 * p], e->g[GP_FX1_E2 + 3 * p]);
    for (p = 0; p < 3; ++p) { /* freigewordene Speicher nachtraeglich vergeben */
        gd_fx *f = &e->fx[p];
        if (fx_needs_delay(f->type) && !f->delayRes && g_delayOwner < 0) { g_delayOwner = p; f->delayRes = 1; fx_clear(f); }
        if (f->type == 0 && !f->revRes && g_revOwner < 0) { g_revOwner = p; f->revRes = 1; fx_clear(f); }
    }
}

void gd_engine_all_off(gd_engine *e)
{
    int p, k;
    for (p = 0; p < GD_PARTS; ++p) {
        for (k = 0; k < GD_MAX_VOICES; ++k) { e->parts[p].v[k].active = 0; e->parts[p].v[k].held = 0; }
        e->parts[p].rolling = 0;
    }
}

static float roll_interval(const gd_engine *e) { return e->sr * 60.0f / e->bpm / 8.0f; }

void gd_engine_note_on(gd_engine *e, int part, int note, int vel127)
{
    gd_part *P;
    gd_voice *v;
    int idx = 0, i, kbd, shaped, sample;
    if (part < 0 || part >= GD_PARTS || part == GD_ACCENT) return;
    P = &e->parts[part];
    kbd = gd_is_kbd(part);
    if (kbd) {
        idx = -1;
        for (i = 0; i < GD_MAX_VOICES; ++i) if (!P->v[i].active) { idx = i; break; }
        if (idx < 0) {
            idx = 0;
            for (i = 1; i < GD_MAX_VOICES; ++i) if (P->v[i].age < P->v[idx].age) idx = i;
        }
    }
    v = &P->v[idx];
    memset(v, 0, sizeof *v);
    v->active = 1;
    v->held = 1;
    v->note = note;
    v->vel = (float)gd_clampi(vel127, 1, 127) / 127.0f;
    v->age = ++P->ageCtr;
    v->accent = (uint8_t)(part != GD_AUDIOIN && v->vel >= 0.84f);
    v->nat = v->fenv = v->menv = v->pmenv = 1.0f;
    if (v->accent && P->p[PF_ACCSWEEP] > 0.001f) P->sweep = gd_minf(1.5f, P->sweep + 0.6f);
    v->rng = ((uint32_t)note * 2654435761u + P->ageCtr * 97u + 1u) | 1u;
    v->semis = (kbd && P->glideCoef < 1.0f) ? P->lastNote : (float)note;
    P->lastNote = (float)note;
    sample = part <= 8 ? gd_roundi(P->p[PF_SRC]) == 0 : gd_roundi(P->p[PF_SRC]) == 1;
    (void)sample; /* keine Samples auf dem FM-1: Sample-Quelle klingt wie Synth */
    shaped = kbd || P->p[PF_AMPEG] > 0.5f || part <= 8;
    v->amp = shaped ? 0.0f : 1.0f;
    if (part < 13) {
        const float hp = sdef(part)->hp;
        v->hpA = hp > 0.0f ? 1.0f - gd_exp(-GD_TWO_PI * hp / e->sr) : 0.0f;
    }
    P->rolling = 1;
    P->rollNote = note;
    P->rollVel = v->vel;
    P->rollCnt = roll_interval(e);
    if (P->p[PF_MODBAR] < 0.5f) { P->lfoPh = 0.0f; P->lfoSh = gd_rnd(&P->rng); }
    if (P->p[PF_PMBAR] < 0.5f) P->plfoPh = 0.0f;
    e->activity |= 1u << part;
}

void gd_engine_note_off(gd_engine *e, int part, int note)
{
    gd_part *P;
    int k, kbd, any = 0;
    if (part < 0 || part >= GD_PARTS || part == GD_ACCENT) return;
    P = &e->parts[part];
    kbd = gd_is_kbd(part);
    for (k = 0; k < GD_MAX_VOICES; ++k) {
        gd_voice *v = &P->v[k];
        if (!v->active || !v->held) continue;
        if (kbd && v->note != note) continue;
        v->held = 0;
        if (kbd && P->p[PF_AMPEG] <= 0.5f) v->stage = 2;
        if (!kbd) break;
    }
    for (k = 0; k < GD_MAX_VOICES; ++k) if (P->v[k].active && P->v[k].held) any = 1;
    P->rolling = (uint8_t)any;
}

typedef struct { float g, tb, is; } tube_k;
static tube_k tube_coef(float drive)
{
    tube_k k;
    k.g = 1.0f + 11.0f * drive;
    k.tb = gd_tanh(0.3f * drive);
    k.is = 1.0f / gd_sqrt(k.g);
    return k;
}
static inline float tube_stage(float *xs, float x, float drive, const tube_k *k, float *yp)
{
    const float y = (gd_tanh(k->g * x + 0.3f * drive) - k->tb) * k->is;
    const float o = y - *xs + 0.995f * *yp;
    *xs = y;
    *yp = o;
    return o;
}


/* LFO-Verlauf eines Parts fuer einen Block (alle Stimmen des Parts teilen ihn) */
typedef struct { float ph[GD_BLK], sh[GD_BLK], sm[GD_BLK], pph[GD_BLK], psh[GD_BLK]; } lfo_blk;

static void lfo_run(gd_engine *e, gd_part *P, float beat0, float binc, int c, lfo_blk *L)
{
    static const float barLen[7] = { 1, 2, 4, 8, 16, 32, 64 };
    const float *q = P->p;
    const int mbar = q[PF_MODBAR] > 0.5f, mloop = q[PF_MODLOOP] > 0.5f;
    const int pbar = q[PF_PMBAR] > 0.5f, ploop = q[PF_PMLOOP] > 0.5f;
    const float mlen = barLen[gd_clampi((int)(gd_clampf(q[PF_MODSPEED], 0.0f, 1.0f) * 7.0f), 0, 6)];
    const float plen = barLen[gd_clampi((int)(gd_clampf(q[PF_PMSPEED], 0.0f, 1.0f) * 7.0f), 0, 6)];
    const float hz = mbar ? e->bpm / 60.0f / mlen : P->lfoRate;
    const float smk = gd_minf(1.0f, GD_TWO_PI * hz / e->sr);
    const float minc = P->lfoRate / e->sr, pinc = P->plfoRate / e->sr;
    int i;
    for (i = 0; i < c; ++i) {
        const float beat = beat0 + binc * (float)i;
        if (mbar) {
            const float t = beat / mlen;
            if (mloop) { const float f = t - gd_floorf(t); if (f < P->lfoPh) P->lfoSh = gd_rnd(&P->rng); P->lfoPh = f; }
            else P->lfoPh = t >= 1.0f ? 0.9999f : t;
        } else {
            P->lfoPh += minc;
            if (P->lfoPh >= 1.0f) { if (mloop) { P->lfoPh -= gd_floorf(P->lfoPh); P->lfoSh = gd_rnd(&P->rng); } else P->lfoPh = 0.9999f; }
        }
        P->lfoSm += smk * (P->lfoSh - P->lfoSm);
        if (pbar) {
            const float t = beat / plen;
            if (ploop) { const float f = t - gd_floorf(t); if (f < P->plfoPh) P->plfoSh = gd_rnd(&P->rng); P->plfoPh = f; }
            else P->plfoPh = t >= 1.0f ? 0.9999f : t;
        } else {
            P->plfoPh += pinc;
            if (P->plfoPh >= 1.0f) { if (ploop) { P->plfoPh -= gd_floorf(P->plfoPh); P->plfoSh = gd_rnd(&P->rng); } else P->plfoPh = 0.9999f; }
        }
        L->ph[i] = P->lfoPh; L->sh[i] = P->lfoSh; L->sm[i] = P->lfoSm; L->pph[i] = P->plfoPh; L->psh[i] = P->plfoSh;
    }
}

/* Eine Stimme fuer c Samples; addiert auf pl/pr. Alle blockkonstanten Werte werden vorab bestimmt. */
static void render_voice_block(gd_engine *e, gd_part *P, int part, gd_voice *v, const lfo_blk *L, float *pl, float *pr, int c)
{
    const float *q = P->p;
    const int kbd = gd_is_kbd(part);
    const float sr = e->sr, dt = 1.0f / sr;
    const int er = part <= 8;
    const float depth = (q[PF_MODDEPTH] - 0.5f) * 2.0f;
    const int modOn = gd_absf(depth) > 0.001f;
    const int modNoise = q[PF_MODNOISE] > 0.5f, modSine = q[PF_MODSINE] > 0.5f, mtype = gd_roundi(q[PF_MODTYPE]);
    const int dest = modOn ? (part == GD_AUDIOIN ? gd_clampi(gd_roundi(q[PF_MODDEST]), 0, 2) + 1 : gd_clampi(gd_roundi(q[PF_MODDEST]), 0, 3)) : -1;
    const float pd = (q[PF_PMDEPTH] - 0.5f) * 2.0f;
    const int pmOn = gd_absf(pd) > 0.001f, pmtype = gd_roundi(q[PF_PMTYPE]);
    const float pscale = er ? 48.0f : 12.0f;
    const int ftype = gd_clampi(gd_roundi(q[PF_FTYPE]), 0, 3);
    const int fmodel = ftype == 0 ? gd_clampi(gd_roundi(q[PF_FLTMODEL]), 0, 2) : 0;
    const int ampEg = q[PF_AMPEG] > 0.5f || part <= 8;
    const float lb = gd_clampf(q[PF_LOWBOOST], 0.0f, 1.0f);
    const int dist = kbd && q[PF_DIST] > 0.5f;
    const float od = gd_minf(q[PF_OVERDRIVE], 1.0f);
    const float odD = 1.0f + od * 14.0f, odG = 0.45f + 0.55f / odD, mufMix = gd_minf(1.0f, od * 1.5f);
    const int muffler = q[PF_MUFFLER] > 0.5f;
    const float gain = gd_maxf(0.0f, q[PF_LEVEL]) * 0.5f * 1.41421356f;
    const float pan0 = q[PF_PAN];
    const float gL0 = gain * gd_cos01(gd_clampf(pan0, 0.0f, 1.0f) * 0.25f), gR0 = gain * gd_sin01(gd_clampf(pan0, 0.0f, 1.0f) * 0.25f);
    const float fcoef = v->accent ? P->accCoef : P->decayCoef;
    const float trk = kbd ? q[PF_FLTTRACK] : 0.0f;
    const float egIntB = (q[PF_EGINT] - 0.5f) * 2.0f * (1.0f - 0.6f * trk) * (v->accent ? 1.0f + 0.5f * q[PF_ACCSWEEP] : 1.0f);
    const float accSw = q[PF_ACCSWEEP], fltFM = q[PF_FLTFM], reso = q[PF_RESO];
    /* Keyboard-Oszillatoren */
    const int w1 = gd_clampi(gd_roundi(q[PF_OSC1]), 0, 2), w2 = gd_clampi(gd_roundi(q[PF_OSC2]), 0, 2);
    const int omode = gd_clampi(gd_roundi(q[PF_OSCMOD]), 0, 3);
    const float bal = gd_clampf(q[PF_OSCBAL], 0.0f, 1.0f);
    const float ga = gd_minf(1.0f, 2.0f * (1.0f - bal)), gb = gd_minf(1.0f, 2.0f * bal);
    const int tri = gd_roundi(q[PF_SRC]) == 2;
    const synth_def *def = part < 13 ? sdef(part) : sdef(0);
    float sweep = P->sweep;
    float semisTgt = (float)v->note, f = 0.0f, f2 = 0.0f, lastPitch = -1e9f;
    int i, ended = 0;

    for (i = 0; i < c; ++i) {
        float mod = 0.0f, modPitch = 0.0f, s = 0.0f, pre;
        sweep *= P->sweepCoef;
        v->menv *= P->menvCoef;
        if (modOn) {
            float lv;
            const float ph = L->ph[i];
            if (modNoise) lv = er ? gd_rnd(&v->rng) : L->sm[i];
            else if (modSine) lv = gd_sin01(ph);
            else switch (mtype) {
                case 0: lv = 2.0f * ph - 1.0f; break;
                case 1: lv = ph < 0.5f ? 1.0f : -1.0f; break;
                case 2: lv = ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph; break;
                case 3: lv = L->sh[i]; break;
                default: lv = v->menv; break;
            }
            mod = lv * depth;
            if (dest == 0) modPitch = mod * pscale;
        }
        v->pmenv *= P->pmCoef;
        if (pmOn) {
            const float pp = L->pph[i];
            float lv;
            switch (pmtype) {
                case 0: lv = 2.0f * pp - 1.0f; break;
                case 1: lv = pp < 0.5f ? 1.0f : -1.0f; break;
                case 2: lv = pp < 0.5f ? 4.0f * pp - 1.0f : 3.0f - 4.0f * pp; break;
                case 3: lv = L->psh[i]; break;
                case 4: lv = v->pmenv; break;
                case 6: lv = gd_sin01(pp); break;
                default: lv = gd_rnd(&v->rng); break;
            }
            modPitch += lv * pd * pscale;
        }

        if (kbd) {
            float d1, o1, o2;
            int wrap;
            if (P->glideCoef < 1.0f) v->semis += (semisTgt - v->semis) * P->glideCoef;
            else v->semis = semisTgt;
            if (v->semis + modPitch != lastPitch) {   /* exp2 nur bei Tonhoehenaenderung */
                lastPitch = v->semis + modPitch;
                f = 440.0f * gd_exp2((lastPitch - 69.0f) * (1.0f / 12.0f));
                f2 = f * P->osc2Ratio;
            }
            d1 = f * dt;
            if (omode == 3) d1 *= gd_exp2(v->o2 * bal * 3.0f);
            v->ph1 += d1;
            wrap = v->ph1 >= 1.0f;
            if (wrap) v->ph1 -= gd_floorf(v->ph1);
            if (omode == 2 && wrap) v->ph2 = 0.0f;
            else { v->ph2 += f2 * dt; if (v->ph2 >= 1.0f) v->ph2 -= gd_floorf(v->ph2); }
            o1 = ea_osc(w1, v->ph1, d1);
            o2 = ea_osc(w2, v->ph2, f2 * dt);
            v->o2 = o2;
            if (omode == 1) s = 0.8f * (ga * o1) * (gb * o2);
            else if (omode == 3) s = 0.5f * o1;
            else s = 0.45f * (ga * o1 + gb * o2);
        } else if (part <= 3) {
            float fHz = P->erBase;
            if (modPitch != 0.0f) fHz *= gd_exp2(modPitch * (1.0f / 12.0f));
            fHz = gd_minf(fHz, sr * 0.45f);
            v->ph1 += fHz * dt;
            if (v->ph1 >= 1.0f) v->ph1 -= gd_floorf(v->ph1);
            s = tri ? tri01(v->ph1) : gd_sin01(v->ph1);
        } else {
            float ratio = P->pitchRatio, wn, hpn;
            if (modPitch != 0.0f) ratio *= gd_exp2(modPitch * (1.0f / 12.0f));
            wn = gd_rnd(&v->rng);
            hpn = wn;
            if (def->hp > 0.0f) { v->nlp += v->hpA * (wn - v->nlp); hpn = wn - v->nlp; }
            switch (def->kind) {
                case S_CLAP: {
                    float burst = 1.0f;
                    if (v->t < 0.035f) { const float tm = v->t - 0.011f * gd_floorf(v->t * (1.0f / 0.011f)); burst = tm < 0.006f ? 1.0f : 0.35f; }
                    s = hpn * burst;
                    break;
                }
                case S_PAD: {
                    const float d1 = def->f0 * ratio * dt;
                    v->ph1 += d1; if (v->ph1 >= 1.0f) v->ph1 -= gd_floorf(v->ph1);
                    v->ph2 += d1 * 1.006f; if (v->ph2 >= 1.0f) v->ph2 -= gd_floorf(v->ph2);
                    s = 0.4f * (2.0f * v->ph1 - 1.0f - poly_blep(v->ph1, d1)) + 0.4f * (2.0f * v->ph2 - 1.0f - poly_blep(v->ph2, d1 * 1.006f));
                    break;
                }
                case S_METAL: {
                    static const float mr[6] = { 1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f };
                    const float base = def->f0 * ratio * dt;
                    float m = 0.0f;
                    int k;
                    for (k = 0; k < 6; ++k) {
                        float ph = v->mph[k] + base * mr[k];
                        if (ph >= 1.0f) ph -= gd_floorf(ph);
                        v->mph[k] = ph;
                        m += ph < 0.5f ? 1.0f : -1.0f;
                    }
                    s = m * 0.1f * def->tone + hpn * def->noise * 0.7f;
                    break;
                }
                case S_GLITCH:
                    s = hpn * ((((int)(v->t * 30.0f)) & 1) ? 1.0f : 0.15f);
                    break;
                default:
                    s = hpn * def->noise;
                    break;
            }
            if (part > 8) {
                v->nat *= P->natCoef;
                s *= v->nat;
                if (v->nat < 0.0005f) ended = 1;
            }
        }
        v->t += dt;

        v->fenv *= fcoef;
        if (v->fcnt-- <= 0) {
            const float sw = sweep * accSw;
            float oct = egIntB * v->fenv * 6.0f, res, fc;
            v->fcnt = 7;
            if (dest == 1) oct += mod * 4.0f;
            if (kbd) oct += trk * (v->semis - 60.0f) * (1.0f / 12.0f);
            oct += sw * 2.5f + fltFM * v->fmOut * 3.0f;
            res = gd_minf(1.0f, reso + 0.35f * sw);
            fc = P->cutBase * gd_exp2(oct);
            if (fmodel > 0) ladder_set(&v->lad, fc, res, sr, fmodel);
            else svf_set(&v->svf, fc, res, sr);
        }
        s = fmodel > 0 ? ladder_process(&v->lad, s, fmodel) : svf_process(&v->svf, s, ftype);
        v->fmOut = s;

        if (ampEg || kbd) {
            if (v->stage == 0) {
                v->amp += dt * 500.0f;
                if (v->amp >= 1.0f) { v->amp = 1.0f; v->stage = 1; }
            } else if (v->stage == 1) {
                if (ampEg) { v->amp *= P->ampCoef; if (v->amp < 0.0005f) ended = 1; }
            } else {
                v->amp *= e->relCoef;
                if (v->amp < 0.0005f) ended = 1;
            }
        }
        pre = s * v->amp * v->vel;
        if (dest == 2) pre *= gd_maxf(0.0f, 1.0f + 0.5f * mod);
        if (lb > 0.001f) {
            v->lbLp += P->lbA * (pre - v->lbLp);
            v->lbLp2 += P->lbA * (v->lbLp - v->lbLp2);
            pre = 1.6f * gd_tanh((pre + P->lbG * v->lbLp2) * 0.625f);
        }
        if (dist) pre = gd_tanh(5.0f * pre) * 0.5f;
        if (od > 0.001f) {
            pre = gd_tanh(pre * odD) * odG;
            if (muffler) { v->mufLp += P->mufA * (pre - v->mufLp); pre += mufMix * (v->mufLp - pre); }
        }
        if (dest == 3) {
            const float pn = gd_clampf(pan0 + mod * 0.5f, 0.0f, 1.0f);
            pl[i] += pre * gain * gd_cos01(pn * 0.25f);
            pr[i] += pre * gain * gd_sin01(pn * 0.25f);
        } else {
            pl[i] += pre * gL0;
            pr[i] += pre * gR0;
        }
        if (ended) { v->active = 0; break; }
    }
}

/* Teil 1: alle Stimmen eines Blocks (max. GD_BLK Samples) auf die vier Busse (trocken, FX1-3) mischen */
void gd_engine_render_voices(gd_engine *e, gd_bus *bus, int c)
{
    const float binc = e->bpm / (60.0f * e->sr);
    int part, k, i;
    lfo_blk L;
    if (c > GD_BLK) c = GD_BLK;
    bus->c = c;
    memset(bus->b, 0, sizeof bus->b);
    for (part = 0; part < GD_PARTS; ++part) {
        gd_part *P;
        const int nv = gd_is_kbd(part) ? GD_MAX_VOICES : 1;
        int any = 0, b;
        float pl[GD_BLK], pr[GD_BLK];
        if (part == GD_ACCENT) continue;
        P = &e->parts[part];
        for (k = 0; k < nv; ++k) if (P->v[k].active) { any = 1; break; }
        if (!any && !P->rolling) {
            if (P->sweep > 1e-6f) for (i = 0; i < c; ++i) P->sweep *= P->sweepCoef;
            continue;
        }
        if (P->rolling && P->p[PF_ROLL] > 0.5f && (P->rollCnt -= (float)c) <= 0.0f)
            gd_engine_note_on(e, part, P->rollNote, gd_roundi(P->rollVel * 127.0f));
        lfo_run(e, P, e->beatPos, binc, c, &L);
        for (i = 0; i < c; ++i) pl[i] = pr[i] = 0.0f;
        for (k = 0; k < nv; ++k) if (P->v[k].active) render_voice_block(e, P, part, &P->v[k], &L, pl, pr, c);
        for (i = 0; i < c; ++i) P->sweep *= P->sweepCoef;
        if (P->p[PF_TUBE] > 0.001f) {
            const tube_k tk = tube_coef(P->p[PF_TUBE]);
            for (i = 0; i < c; ++i) {
                pl[i] = tube_stage(&P->xl, pl[i], P->p[PF_TUBE], &tk, &P->yl);
                pr[i] = tube_stage(&P->xr, pr[i], P->p[PF_TUBE], &tk, &P->yr);
            }
        }
        b = P->p[PF_FXSEND] > 0.5f ? 1 + gd_clampi(gd_roundi(P->p[PF_FXSEL]), 0, 2) : 0;
        for (i = 0; i < c; ++i) { bus->b[b][0][i] += pl[i]; bus->b[b][1][i] += pr[i]; }
    }
    e->beatPos += binc * (float)c;
}

/* Teil 2: FX-Kette, Soundgoodizer, Master-Tube und Master-Lautstaerke; addiert auf l/r.
   Kann auf dem FM-1 auf dem zweiten Kern laufen (Pipeline: Kern 0 rechnet den naechsten Block). */
void gd_engine_render_master(gd_engine *e, const gd_bus *bus, float *l, float *r)
{
    const float master = e->g[GP_TUBEMASTER];
    const float mgTarget = gd_master_gain(e->g[GP_MASTERVOL]);
    const int chain = gd_clampi(gd_roundi(e->g[GP_FXCHAIN]), 0, 3);
    const tube_k mtk = tube_coef(master);
    float pkL = 0.0f, pkR = 0.0f;
    int i, k, inZero = 1;
    for (k = 0; k < 4 && inZero; ++k)
        for (i = 0; i < bus->c; ++i)
            if (bus->b[k][0][i] != 0.0f || bus->b[k][1][i] != 0.0f) { inZero = 0; break; }
    /* Stille: kein Eingang und der Ausgang war laenger als die laengste Delay-Zeile stumm -> FX, Soundgoodizer
       und Tube ueberspringen (Ergebnis waere < -100 dB). Spart im Leerlauf fast die ganze Rechenzeit. */
    if (inZero && e->quiet >= GD_QUIET_SKIP) {
        for (i = 0; i < bus->c; ++i) e->mGain += 0.002f * (mgTarget - e->mGain);
        return;
    }
    for (i = 0; i < bus->c; ++i) {
        float mixL = bus->b[0][0][i], mixR = bus->b[0][1][i], oL, oR;
        float fxL[3], fxR[3];
        for (k = 0; k < 3; ++k) { fxL[k] = bus->b[k + 1][0][i]; fxR[k] = bus->b[k + 1][1][i]; }
        fx_process(&e->fx[0], e->sr, e->bpm, fxL[0], fxR[0], &oL, &oR);
        if (chain == 1 || chain == 3) { fxL[1] += oL; fxR[1] += oR; } else { mixL += oL; mixR += oR; }
        fx_process(&e->fx[1], e->sr, e->bpm, fxL[1], fxR[1], &oL, &oR);
        if (chain == 2 || chain == 3) { fxL[2] += oL; fxR[2] += oR; } else { mixL += oL; mixR += oR; }
        fx_process(&e->fx[2], e->sr, e->bpm, fxL[2], fxR[2], &oL, &oR);
        mixL += oL;
        mixR += oR;
        good_process(&e->good, &mixL, &mixR);
        if (master > 0.001f) {
            mixL = tube_stage(&e->mxl, mixL, master, &mtk, &e->myl);
            mixR = tube_stage(&e->mxr, mixR, master, &mtk, &e->myr);
        }
        e->mGain += 0.002f * (mgTarget - e->mGain);
        mixL *= e->mGain;
        mixR *= e->mGain;
        if (gd_absf(mixL) > pkL) pkL = gd_absf(mixL);
        if (gd_absf(mixR) > pkR) pkR = gd_absf(mixR);
        l[i] += mixL;
        r[i] += mixR;
    }
    if (pkL > e->meterL) e->meterL = pkL;
    if (pkR > e->meterR) e->meterR = pkR;
    if (inZero && pkL < 1e-5f && pkR < 1e-5f) { if (e->quiet < GD_QUIET_SKIP) e->quiet += bus->c; }
    else e->quiet = 0;
}

void gd_engine_render(gd_engine *e, float *l, float *r, int n)
{
    gd_bus bus;
    int done = 0;
    while (done < n) {
        const int c = n - done < GD_BLK ? n - done : GD_BLK;
        gd_engine_render_voices(e, &bus, c);
        gd_engine_render_master(e, &bus, l + done, r + done);
        done += c;
    }
}
