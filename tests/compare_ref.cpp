// Vergleich der FM-1-Engine mit der Original-Engine des JUCE-Plugins (GrooveDeck Source/Dsp.h).
// Beide bekommen dieselben Parameter (aus einem Werkspreset) und dieselben Noten (Steps des Patterns, ohne Arp).
// Uebersetzen:  g++ -std=c++17 -O2 -I<GrooveDeck>/Source -Icore tests/compare_ref.cpp core/*.c -o compare_ref
// Aufruf:       compare_ref [erstes] [letztes]   -> Pegelabweichung (dB) und Korrelation je Preset
#include "Dsp.h"
extern "C" {
#include "gd_preset.h"
}
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <memory>

static const int SR = 44100;

struct Ref
{
    gdsp::Engine eng;
    std::atomic<float> ctl[16][20], src[16], tube[16], ext[16][26], fxp[3][4], fxChain, tubeMaster, goodOn, goodAmt, goodStyle, goodAuto, masterVol;
    void bind (const gd_engine& e, bool dry)
    {
        for (int p = 0; p < 16; ++p)
        {
            for (int c = 0; c < 20; ++c) { ctl[p][c] = e.parts[p].p[c]; eng.ctl[p][c] = &ctl[p][c]; }
            if (dry) ctl[p][gdsp::cFxSend] = 0.0f;
            src[p] = e.parts[p].p[PF_SRC]; eng.src[p] = &src[p];
            tube[p] = e.parts[p].p[PF_TUBE]; eng.tube[p] = &tube[p];
            for (int k = 0; k < 26; ++k) { ext[p][k] = e.parts[p].p[PF_X0 + k]; eng.ext[p][k] = &ext[p][k]; }
        }
        for (int f = 0; f < 3; ++f)
        {
            fxp[f][0] = e.g[GP_FX1_TYPE + 3 * f]; fxp[f][1] = e.g[GP_FX1_E1 + 3 * f]; fxp[f][2] = e.g[GP_FX1_E2 + 3 * f]; fxp[f][3] = 0;
            for (int w = 0; w < 4; ++w) eng.fxp[f][w] = &fxp[f][w];
        }
        fxChain = e.g[GP_FXCHAIN]; eng.fxChain = &fxChain;
        tubeMaster = e.g[GP_TUBEMASTER]; eng.tubeMaster = &tubeMaster;
        goodOn = e.g[GP_GOODON]; eng.goodOn = &goodOn;
        goodAmt = e.g[GP_GOODAMT]; eng.goodAmt = &goodAmt;
        goodStyle = e.g[GP_GOODSTYLE]; eng.goodStyle = &goodStyle;
        goodAuto = e.g[GP_GOODAUTO]; eng.goodAuto = &goodAuto;
        masterVol = e.g[GP_MASTERVOL]; eng.masterVol = &masterVol;
        eng.prepare (SR);
        eng.setTempo (e.g[GP_TEMPO]);
        eng.setBeatPos (0.0);
    }
};

struct Ev { long pos; int part, note, vel; bool on; };

static gd_engine me;
static gd_seq seq;

int main (int argc, char** argv)
{
    const int first = argc > 1 ? std::atoi (argv[1]) : 0, last = argc > 2 ? std::atoi (argv[2]) : gd_num_presets - 1;
    const int dryMode = argc > 3 ? std::atoi (argv[3]) : 0;
    double worstDb = 0; int worstIdx = -1;
    for (int idx = first; idx <= last; ++idx)
    {
        gd_engine_init (&me, SR);
        gd_seq_init (&seq, &me);
        gd_preset_load (&seq, idx);
        if (dryMode) for (int p = 0; p < 16; ++p) me.parts[p].p[PF_FXSEND] = 0.0f;
        if (const char* so = std::getenv ("SOLO"))   // nur ein Part, ohne Soundgoodizer und Master-Tube
        {
            const int sp = std::atoi (so);
            for (int p = 0; p < 16; ++p) if (p != sp) me.parts[p].p[PF_LEVEL] = 0.0f;
            me.g[GP_GOODON] = 0.0f; me.g[GP_TUBEMASTER] = 0.0f;
        }
        gd_engine_update (&me);
        auto refp = std::make_unique<Ref>();   // frisch je Preset (die Original-Engine behaelt sonst Zustand)
        Ref& ref = *refp;
        ref.bind (me, dryMode != 0);
        // Noten fuer 2 Durchlaeufe des Patterns
        const double spb = SR * 60.0 / me.g[GP_TEMPO];
        const int pl = seq.pat.patLen;
        const float gate = me.g[GP_GATE];
        std::vector<Ev> ev;
        for (int s = 0; s < pl * 2; ++s)
        {
            const int step = s % pl;
            const int vel = ((seq.pat.stepMask[13] >> step) & 1u) ? 127 : 100;
            for (int p = 0; p < 16; ++p)
            {
                if (p == 13 || !((seq.pat.stepMask[p] >> step) & 1u)) continue;
                int note = 60, len = 1;
                if (p >= 14) { note = seq.pat.kbdNote[p - 14][step]; len = seq.pat.kbdLen[p - 14][step]; }
                const long on = (long) std::floor (s * 0.25 * spb), off = (long) std::floor ((s * 0.25 + ((len - 1) + gate) * 0.25) * spb);
                ev.push_back ({ on, p, note, vel, true });
                ev.push_back ({ off, p, note, 0, false });
            }
        }
        const long total = (long) (pl * 2 * 0.25 * spb) + SR;
        std::vector<float> aL (total, 0.f), aR (total, 0.f), bL (total, 0.f), bR (total, 0.f);
        // FM-1-Engine: Ereignisse sampelgenau anwenden
        {
            std::vector<Ev> sorted = ev;
            std::stable_sort (sorted.begin(), sorted.end(), [] (const Ev& x, const Ev& y) { return x.pos < y.pos; });
            long t = 0; size_t k = 0;
            while (t < total)
            {
                while (k < sorted.size() && sorted[k].pos <= t)
                {
                    if (sorted[k].on) gd_engine_note_on (&me, sorted[k].part, sorted[k].note, sorted[k].vel);
                    else gd_engine_note_off (&me, sorted[k].part, sorted[k].note);
                    ++k;
                }
                long next = k < sorted.size() ? sorted[k].pos : total;
                if (next > total) next = total;
                if (next == t) next = t + 1;
                me.beatPos = (float) (t / spb);
                gd_engine_render (&me, &aL[t], &aR[t], (int) (next - t));
                t = next;
            }
        }
        // Original-Engine in Bloecken zu 256
        for (long t = 0; t < total; t += 256)
        {
            const int n = (int) std::min<long> (256, total - t);
            std::vector<gdsp::Engine::Evt> be;
            for (auto& e : ev) if (e.pos >= t && e.pos < t + n) be.push_back ({ (int) (e.pos - t), e.part, e.note, e.vel, e.on });
            ref.eng.setBeatPos (t / spb);
            ref.eng.render (&bL[t], &bR[t], n, be.data(), (int) be.size());
        }
        if (std::getenv ("SOLO"))
            for (long i = 0; i < total; ++i)
                if (std::fabs (aL[i] - bL[i]) > 1e-3f) { std::printf ("erste Abweichung bei Sample %ld (%.3f s): %f / %f\n", i, (double) i / SR, aL[i], bL[i]); break; }
        double ea = 0, eb = 0, ab = 0;
        for (long i = 0; i < total; ++i) { ea += aL[i] * aL[i] + aR[i] * aR[i]; eb += bL[i] * bL[i] + bR[i] * bR[i]; ab += aL[i] * bL[i] + aR[i] * bR[i]; }
        const double db = 10.0 * std::log10 ((ea + 1e-12) / (eb + 1e-12));
        const double corr = ab / std::sqrt ((ea + 1e-12) * (eb + 1e-12));
        if (std::fabs (db) > std::fabs (worstDb)) { worstDb = db; worstIdx = idx; }
        std::printf ("%3d %-28s Pegel %+6.2f dB  Korrelation %.3f\n", idx, gd_presets[idx].name, db, corr);
    }
    std::printf ("Groesste Pegelabweichung: %+.2f dB (Preset %d)\n", worstDb, worstIdx);
    return 0;
}
