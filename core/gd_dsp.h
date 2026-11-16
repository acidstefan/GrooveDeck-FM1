/* GrooveDeck FM-1 - Klangerzeugung (Portierung von GrooveDeck Source/Dsp.h nach C99).
   Signalkette je Kanal wie im Plugin: Quelle -> Filter (SVF oder 303-Diode/Leiter) + Filter-EG -> Amp-EG
   -> Low Boost / Distortion / Overdrive -> Level/Pan -> Tube -> FX-Send. Summe -> FX 1-3 -> Soundgoodizer
   (Auto-Pegel) -> Master-Tube -> Master-Lautstaerke.
   Unterschiede zum Plugin: keine Samples (alle Werkspresets nutzen nur die Synth-Quellen), FX-Puffer in
   16 Bit und nur ein Hall- und ein Delay-Speicher fuer alle drei FX-Einheiten (siehe gd_config.h). */
#ifndef GD_DSP_H
#define GD_DSP_H
#include <stdint.h>
#include "gd_config.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { GD_PARTS = 16, GD_ACCENT = 13, GD_AUDIOIN = 12, GD_KBD1 = 14, GD_KBD2 = 15 };
static inline int gd_is_kbd(int p) { return p >= GD_KBD1; }

/* Parameter je Kanal (Rohwerte wie im Plugin). Reihenfolge: 20 ESX-Regler, 26 Extras, Quelle, Tube, 20 Arp-Werte */
enum {
    PF_START = 0, PF_REVERSE, PF_PITCH, PF_FTYPE, PF_CUTOFF, PF_RESO, PF_EGINT, PF_LEVEL, PF_PAN, PF_EGTIME,
    PF_AMPEG, PF_ROLL, PF_FXSEND, PF_FXSEL, PF_MODTYPE, PF_MODDEPTH, PF_MODSPEED, PF_MODDEST, PF_MODSYNC, PF_MOTION,
    PF_X0, /* Extras ab hier, Reihenfolge wie gdsp::ExtraCtl */
    PF_WAVE = PF_X0, PF_LOWBOOST, PF_MODNOISE, PF_OSC1, PF_OSC2, PF_OSC2P, PF_OSCMOD, PF_OSCBAL, PF_DIST,
    PF_PMTYPE, PF_PMDEPTH, PF_PMSPEED, PF_AMPDECAY,
    PF_FLTMODEL, PF_FLTTRACK, PF_FLTFM, PF_ACCDECAY, PF_ACCSWEEP, PF_SWEEPSPD, PF_OVERDRIVE, PF_MUFFLER,
    PF_MODSINE, PF_PMBAR, PF_PMLOOP, PF_MODBAR, PF_MODLOOP,
    PF_SRC, PF_TUBE,
    PF_A0, /* Arpeggiator / Chord (nur Keyboard 1/2), Reihenfolge wie garp::Key */
    PF_ARP_ON = PF_A0, PF_ARP_HOLD, PF_ARP_STYLE, PF_ARP_GRID, PF_ARP_OCT, PF_ARP_GATE, PF_ARP_SHUFFLE, PF_ARP_ACCENT,
    PF_CHORD_ON, PF_CHORD_FORM, PF_CHORD_VOICING, PF_CHORD_LEN, PF_SLOT0,
    PF_COUNT = PF_SLOT0 + 8
};

/* globale Parameter */
enum {
    GP_TEMPO = 0, GP_GATE, GP_FXCHAIN, GP_TUBEMASTER, GP_GOODON, GP_GOODAMT, GP_GOODSTYLE, GP_GOODAUTO, GP_MASTERVOL,
    GP_FX1_TYPE, GP_FX1_E1, GP_FX1_E2, GP_FX2_TYPE, GP_FX2_E1, GP_FX2_E2, GP_FX3_TYPE, GP_FX3_E1, GP_FX3_E2,
    GP_COUNT
};

typedef struct {
    float ic1, ic2, a1, a2, a3, k;
} gd_svf;

typedef struct {
    float s[4];
    float g, k, comp, open;
} gd_ladder;

typedef struct {
    uint8_t active, held, accent, stage;
    int note;
    float vel;
    unsigned age;
    float t;
    float ph1, ph2, mph[6];
    float amp, nat, fenv, menv, pmenv, semis, nlp, hpA, lbLp, lbLp2, o2, fmOut, mufLp;
    uint32_t rng;
    int fcnt;
    gd_svf svf;
    gd_ladder lad;
} gd_voice;

typedef struct {
    gd_voice v[GD_MAX_VOICES];
    unsigned ageCtr;
    float p[PF_COUNT];
    /* abgeleitete Koeffizienten (gd_part_update) */
    float sweep, sweepCoef, accCoef, mufA, decayCoef, natCoef, glideCoef, menvCoef, ampCoef, pmCoef, lbA, lbG;
    float lfoRate, plfoRate, cutBase, erBase, osc2Ratio, pitchRatio;
    float lfoPh, plfoPh, lfoSh, plfoSh, lfoSm;
    uint32_t rng;
    uint8_t rolling;
    int rollNote;
    float rollVel, rollCnt, lastNote;
    float xl, yl, xr, yr;
} gd_part;

typedef struct gd_fx gd_fx;
struct gd_fx {
    int type, fcnt, decCnt, delayRes, revRes, wp;
    int ci[2][8], ai[2][4];
    float clp[2][8];
    float e1, e2, env, decL, decR, eqE1, eqE2;
    float lfo, grainPh, psPh, grainOff[2];
    uint32_t rng;
    float phZ[2][4], phFb[2];
    float bq[5], bqZ[2][2];
    gd_svf formA, formB, filt, svfA[2], svfB[2], svfF[2];
};

typedef struct {
    uint8_t on, autoLvl, half;
    float prevM;
    float amt;
    int style, cnt, lvCnt;
    float pIn, pOut, pC, ga;
    gd_svf bp[12];
    float e[12], eC, aLow, aHigh, lpL[2], lpH[2], env, gr, gLo, gHi, kLo, kHi, makeup, satD, satK;
    float levelDb, reduction;
    float bands[12]; /* Analyseanzeige 0..1 */
} gd_good;

typedef struct {
    float sr, bpm, beatPos, relCoef;
    float g[GP_COUNT];
    gd_part parts[GD_PARTS];
    gd_fx fx[3];
    gd_good good;
    float mGain, mxl, myl, mxr, myr;
    float meterL, meterR;
    uint32_t activity; /* Bit je Part: Note seit letztem Abholen */
} gd_engine;

#define GD_BLK 32
typedef struct { float b[4][2][GD_BLK]; int c; } gd_bus; /* Bus 0 trocken, 1-3 FX-Sends */

/* Standardwerte wie GrooveDeckProcessor::resetPatch (masterVol bleibt erhalten) */
void gd_engine_init(gd_engine *e, float sampleRate);
void gd_engine_reset_patch(gd_engine *e);
float gd_part_default(int part, int field);
float gd_global_default(int id);
void gd_engine_update(gd_engine *e); /* nach Parameteraenderungen aufrufen (pro Block genuegt) */
void gd_engine_note_on(gd_engine *e, int part, int note, int vel127);
void gd_engine_note_off(gd_engine *e, int part, int note);
void gd_engine_all_off(gd_engine *e);
/* addiert n Stereo-Samples auf l/r */
void gd_engine_render(gd_engine *e, float *l, float *r, int n);
/* dasselbe in zwei Haelften (fuer zwei Kerne): Stimmen -> Busse, Busse -> FX/Master */
void gd_engine_render_voices(gd_engine *e, gd_bus *bus, int c);
void gd_engine_render_master(gd_engine *e, const gd_bus *bus, float *l, float *r);
float gd_master_gain(float v);

#ifdef __cplusplus
}
#endif
#endif
