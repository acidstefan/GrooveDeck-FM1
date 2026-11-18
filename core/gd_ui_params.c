/* GrooveDeck FM-1 - Reglerseiten und Werteanzeige. */
#include "gd_ui_params.h"
#include "gd_math.h"
#include "gd_fmt.h"
#include <string.h>

static const char *const N_FTYPE[] = { "LPF", "HPF", "BPF", "BPF+" };
static const char *const N_FMODEL[] = { "SVF", "303", "LADDER" };
static const char *const N_MTYPE[] = { "SAW", "SQR", "TRI", "S&H", "EG" };
static const char *const N_MDEST[] = { "PITCH", "CUTOFF", "AMP", "PAN" };
static const char *const N_WAVE[] = { "SAW", "SQR", "TRI" };
static const char *const N_OSCMOD[] = { "AUS", "RING", "SYNC", "CROSS" };
static const char *const N_SRC[] = { "SAMPLE", "SINUS", "DREIECK" };
static const char *const N_ONOFF[] = { "AUS", "AN" };
static const char *const N_FXTYPE[] = { "REVERB", "SYNC DLY", "SHORT DLY", "MOD DLY", "GRAIN", "CHO/FLG", "PHASER", "RING MOD",
                                        "TALK MOD", "PITCH", "COMP", "DIST", "DECIM", "EQ", "LPF", "HPF" };
static const char *const N_FXSEL[] = { "FX1", "FX2", "FX3" };
static const char *const N_CHAIN[] = { "KEINE", "1>2", "2>3", "1>2>3" };
static const char *const N_STYLE[] = { "AUSGEW.", "PUNCH", "WARM", "HELL", "BREIT" };
static const char *const N_SWEEP[] = { "FAST", "NORMAL", "SLOW" };

#define P(l, f, k, lo, hi, n) { l, SC_PART, k, f, lo, hi, n }
#define G(l, f, k, lo, hi, n) { l, SC_GLOBAL, k, f, lo, hi, n }
#define C(l, f) P(l, f, PK_CONT, 0.0f, 1.0f, 0)

/* [Seite][Unterseite][Knopf]; Varianten fuer Drum- und Keyboard-Parts bei EDIT */
static const gd_pdesc pg_home[2][4] = {
    { C("CUTOFF", PF_CUTOFF), C("RESO", PF_RESO), C("EG INT", PF_EGINT), C("LEVEL", PF_LEVEL) },
    { C("PAN", PF_PAN), C("EG TIME", PF_EGTIME), C("TUBE", PF_TUBE), { "MUTE", SC_MUTE, PK_SWITCH, 0, 0, 1, N_ONOFF } } };
static const gd_pdesc pg_env[2][4] = {
    { C("CUTOFF", PF_CUTOFF), C("RESO", PF_RESO), C("EG INT", PF_EGINT), C("EG TIME", PF_EGTIME) },
    { P("FLT TYP", PF_FTYPE, PK_CHOICE, 0, 3, N_FTYPE), P("MODELL", PF_FLTMODEL, PK_CHOICE, 0, 2, N_FMODEL),
      C("ACC SWEEP", PF_ACCSWEEP), C("OVERDRIVE", PF_OVERDRIVE) } };
static const gd_pdesc pg_lfo[2][4] = {
    { P("MOD TYP", PF_MODTYPE, PK_CHOICE, 0, 4, N_MTYPE), C("MOD TIEFE", PF_MODDEPTH), C("MOD TEMPO", PF_MODSPEED),
      P("MOD ZIEL", PF_MODDEST, PK_CHOICE, 0, 3, N_MDEST) },
    { P("LFO SINUS", PF_MODSINE, PK_SWITCH, 0, 1, N_ONOFF), P("NOISE", PF_MODNOISE, PK_SWITCH, 0, 1, N_ONOFF),
      P("BPM SYNC", PF_MODSYNC, PK_SWITCH, 0, 1, N_ONOFF), P("TAKT-LFO", PF_MODBAR, PK_SWITCH, 0, 1, N_ONOFF) } };
static const gd_pdesc pg_edit_drum[2][4] = {
    { C("PITCH", PF_PITCH), C("AMP DECAY", PF_AMPDECAY), C("LOW BOOST", PF_LOWBOOST), C("PAN", PF_PAN) },
    { P("QUELLE", PF_SRC, PK_CHOICE, 1, 2, N_SRC), C("TUBE", PF_TUBE), P("ROLL", PF_ROLL, PK_SWITCH, 0, 1, N_ONOFF),
      C("PMOD TIEFE", PF_PMDEPTH) } };
static const gd_pdesc pg_edit_kbd[2][4] = {
    { P("OSC1", PF_OSC1, PK_CHOICE, 0, 2, N_WAVE), P("OSC2", PF_OSC2, PK_CHOICE, 0, 2, N_WAVE), C("OSC2 PITCH", PF_OSC2P),
      C("OSC BAL", PF_OSCBAL) },
    { P("OSC MOD", PF_OSCMOD, PK_CHOICE, 0, 3, N_OSCMOD), C("GLIDE", PF_PITCH), P("DIST", PF_DIST, PK_SWITCH, 0, 1, N_ONOFF),
      C("TRACKING", PF_FLTTRACK) } };
static const gd_pdesc pg_fx[2][4] = {
    { G("FX1 TYP", GP_FX1_TYPE, PK_CHOICE, 0, 15, N_FXTYPE), G("FX1 EDIT1", GP_FX1_E1, PK_CONT, 0, 1, 0),
      G("FX1 EDIT2", GP_FX1_E2, PK_CONT, 0, 1, 0), P("FX SEND", PF_FXSEND, PK_SWITCH, 0, 1, N_ONOFF) },
    { G("FX2 TYP", GP_FX2_TYPE, PK_CHOICE, 0, 15, N_FXTYPE), G("FX2 EDIT1", GP_FX2_E1, PK_CONT, 0, 1, 0),
      G("FX2 EDIT2", GP_FX2_E2, PK_CONT, 0, 1, 0), P("FX WAHL", PF_FXSEL, PK_CHOICE, 0, 2, N_FXSEL) } };
static const gd_pdesc pg_glo[2][4] = {
    { G("TEMPO", GP_TEMPO, PK_TEMPO, 20, 300, 0), { "LAENGE", SC_PATLEN, PK_INT, 0, 1, 64, 0 },
      G("GATE", GP_GATE, PK_CONT, 0.05f, 0.95f, 0), G("GOODIZER", GP_GOODAMT, PK_CONT, 0, 1, 0) },
    { G("GOOD STIL", GP_GOODSTYLE, PK_CHOICE, 0, 4, N_STYLE), G("AUTO-PEGEL", GP_GOODAUTO, PK_SWITCH, 0, 1, N_ONOFF),
      G("MASTER TUBE", GP_TUBEMASTER, PK_CONT, 0, 1, 0), G("FX KETTE", GP_FXCHAIN, PK_CHOICE, 0, 3, N_CHAIN) } };
static const gd_pdesc pg_arp[2][4] = {
    { P("ARP", PF_ARP_ON, PK_SWITCH, 0, 1, N_ONOFF), P("STYLE", PF_ARP_STYLE, PK_STYLE, 0, GA_FACTORY - 1, 0),
      P("GRID", PF_ARP_GRID, PK_GRID, 0, 5, 0), P("OKTAVEN", PF_ARP_OCT, PK_INT, 0, 3, 0) },
    { P("CHORD", PF_CHORD_ON, PK_SWITCH, 0, 1, N_ONOFF), P("AKKORD", PF_CHORD_FORM, PK_FORM, 0, GA_FORMS - 1, 0),
      P("VOICING", PF_CHORD_VOICING, PK_VOICING, 0, GA_VOICINGS - 1, 0), C("ARP GATE", PF_ARP_GATE) } };
static const gd_pdesc pg_sel[3] = {
    { "KANAL-PRESET", SC_CHAN, PK_CHAN, 0, 0, 99, 0 }, { "MUTE", SC_MUTE, PK_SWITCH, 0, 0, 1, N_ONOFF },
    C("LEVEL", PF_LEVEL) };
static const gd_pdesc pg_sel_sweep = P("SWEEP TEMPO", PF_SWEEPSPD, PK_CHOICE, 0, 2, N_SWEEP);

const gd_pdesc *gd_page_param(int page, int sub, int part, int k)
{
    const int kbd = gd_is_kbd(part);
    sub &= 1;
    if (k < 0 || k > 3) return 0;
    if (part == GD_ACCENT && page != PG_GLO && page != PG_FX) return 0; /* Accent-Spur hat keinen Klang */
    if (part == GD_ACCENT && page == PG_FX && k == 3) return 0;
    switch (page) {
        case PG_HOME: return &pg_home[sub][k];
        case PG_ENV: return &pg_env[sub][k];
        case PG_LFO: return &pg_lfo[sub][k];
        case PG_EDIT: return kbd ? &pg_edit_kbd[sub][k] : &pg_edit_drum[sub][k];
        case PG_FX: return &pg_fx[sub][k];
        case PG_GLO: return &pg_glo[sub][k];
        case PG_ARP: return kbd ? &pg_arp[sub][k] : 0;
        case PG_SEL: return k == 3 ? &pg_sel_sweep : &pg_sel[k];
        default: return 0;
    }
}

const char *gd_page_name(int page)
{
    static const char *n[PG_COUNT] = { "HOME", "ENV", "LFO", "EDIT", "FX", "GLOBAL", "ARP/CHORD", "KANAL" };
    return page >= 0 && page < PG_COUNT ? n[page] : "";
}

const char *gd_part_short(int part)
{
    static const char *n[GD_PARTS] = { "BD", "SD", "TM", "PC", "CP", "CH", "OH", "CY", "C2", "S1", "S2", "SL", "AI", "AC", "BS", "LD" };
    return part >= 0 && part < GD_PARTS ? n[part] : "?";
}

void gd_param_format(const gd_app *a, const gd_pdesc *d, char *buf, int cap)
{
    const float v = gd_param_value(a, d);
    const int iv = gd_roundi(v);
    switch (d->kind) {
        case PK_CONT: gd_snprintf(buf, (size_t)cap, "%d", gd_roundi(v * 100.0f)); break;
        case PK_TEMPO: gd_snprintf(buf, (size_t)cap, "%.1f", (double)v); break;
        case PK_INT: gd_snprintf(buf, (size_t)cap, "%d", d->field == PF_ARP_OCT ? iv + 1 : iv); break;
        case PK_CHOICE:
        case PK_SWITCH:
            gd_snprintf(buf, (size_t)cap, "%s", d->names ? d->names[gd_clampi(iv, (int)d->lo, (int)d->hi)] : "?");
            break;
        case PK_STYLE: gd_snprintf(buf, (size_t)cap, "%s", gd_arp_style_name(iv)); break;
        case PK_FORM: gd_snprintf(buf, (size_t)cap, "%s", gd_arp_form_name(iv)); break;
        case PK_GRID: gd_snprintf(buf, (size_t)cap, "%s", gd_arp_grid_name(iv)); break;
        case PK_VOICING: gd_snprintf(buf, (size_t)cap, "%s", gd_arp_voicing_name(iv)); break;
        case PK_CHAN: {
            const int idx = iv >= 0 ? gd_chan_preset_nth(a->part, iv) : -1;
            gd_snprintf(buf, (size_t)cap, "%s", idx >= 0 ? gd_chan_presets[idx].name : "--");
            break;
        }
        default: buf[0] = 0; break;
    }
}

float gd_param_norm(const gd_app *a, const gd_pdesc *d)
{
    float v = gd_param_value(a, d), lo = d->lo, hi = d->hi;
    if (d->kind == PK_CHAN) { hi = (float)(gd_chan_preset_count(a->part) - 1); if (v < 0.0f) return 0.0f; }
    if (hi <= lo) return 0.0f;
    return gd_clampf((v - lo) / (hi - lo), 0.0f, 1.0f);
}
