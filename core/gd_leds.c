/* GrooveDeck FM-1 - LED-Logik: welche Tasten- und Button-LEDs wie hell leuchten.
   Hardwareunabhaengig; die Plattform multiplext die Werte ueber die Tastenmatrix (siehe target/README.md). */
#include "gd_app.h"
#include "gd_math.h"

enum { L_OFF = 0, L_DIM = 40, L_MID = 120, L_ON = 255 };

static uint8_t blink(uint32_t now, unsigned periodMs, uint8_t on, uint8_t off)
{
    return ((now / (periodMs / 2u)) & 1u) ? off : on;
}

void gd_leds_compute(const gd_app *a, uint32_t now, uint8_t keys[GD_KEYS], uint8_t btn[GB_COUNT], uint8_t *playGreen)
{
    static const int pageBtn[PG_COUNT] = { GB_HOME, GB_ENV, GB_LFO, GB_EDIT, GB_FX, GB_GLO, GB_ARP, GB_SEL };
    const gd_seq *s = &a->seq;
    const int part = a->part, kbd = gd_is_kbd(part);
    const int pl = s->pat.patLen, pages = (pl + 15) / 16;
    int k, p;

    /* ------------------------------------------------ Tasten (27) */
    for (k = 0; k < GD_KEYS; ++k) keys[k] = L_OFF;
    if (a->stepMode) {
        /* weisse Tasten: Steps der aktuellen Seite; laufender Step blinkt schnell */
        for (k = 0; k < 16; ++k) {
            const int st = a->stepPage * 16 + k;
            const int on = st < pl && ((s->pat.stepMask[part] >> st) & 1u);
            uint8_t v = on ? L_ON : L_OFF;
            if (s->playing && s->currentStep == st) v = blink(now, 160u, L_ON, on ? L_DIM : L_OFF);
            else if (!on && st < pl && (k & 3) == 0) v = 8; /* Zaehlzeit schwach markiert */
            keys[gd_white_key(k)] = v;
        }
        /* schwarze Tasten: Kanaele - gewaehlter hell (stumm: blinkt), stumme aus, uebrige schwach */
        {
            static const int chan[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, GD_KBD1, GD_KBD2 };
            for (p = 0; p < 11; ++p) {
                const int muted = (int)((s->muteMask >> chan[p]) & 1u);
                uint8_t v = muted ? L_OFF : L_DIM;
                if (chan[p] == part) v = muted ? blink(now, 600u, L_ON, L_OFF) : L_ON;
                keys[gd_black_key(p)] = v;
            }
        }
    } else if (kbd) {
        /* PLAY, Bass/Lead: klingende Noten (auch vom Arpeggiator und Sequencer) */
        const gd_part *P = &a->eng.parts[part];
        const int base = GD_KEY_BASE_NOTE + 12 * a->octave + a->transpose;
        for (k = 0; k < GD_MAX_VOICES; ++k)
            if (P->v[k].active && P->v[k].held) {
                const int key = P->v[k].note - base;
                if (key >= 0 && key < GD_KEYS) keys[key] = L_ON;
            }
    } else {
        /* PLAY, Drums: unterste neun Tasten blitzen beim Ausloesen ihres Drums auf */
        for (p = 0; p < 9; ++p) keys[p] = a->flashMs[p] > 0 ? L_ON : (p == part ? L_DIM : L_OFF);
    }
    for (k = 0; k < GD_KEYS; ++k) if (a->keyPart[k] >= 0) keys[k] = L_ON; /* gedrueckte Tasten immer hell */

    /* ------------------------------------------------ Buttons (14) */
    for (k = 0; k < GB_COUNT; ++k) btn[k] = L_OFF;
    if (a->page >= 0 && a->page < PG_COUNT)
        btn[pageBtn[a->page]] = a->sub ? blink(now, 1000u, L_ON, L_MID) : L_ON; /* Seite B: langsames Pulsieren */
    if (a->page != PG_ARP && (a->eng.parts[GD_KBD1].p[PF_ARP_ON] > 0.5f || a->eng.parts[GD_KBD2].p[PF_ARP_ON] > 0.5f))
        btn[GB_ARP] = L_DIM;                         /* Arp laeuft auf Bass oder Lead */
    btn[GB_SEQ] = a->stepMode ? L_ON : L_OFF;
    btn[GB_PLAY] = s->playing ? L_OFF : L_ON;       /* weisse LED = STOP */
    *playGreen = s->playing ? (a->extClock ? blink(now, 500u, L_ON, L_MID) : L_ON) : L_OFF;
    btn[GB_REC] = a->rec ? (s->playing ? L_ON : blink(now, 1000u, L_ON, L_OFF)) : L_OFF;
    btn[GB_SAVE] = a->btnDown[GB_SAVE] ? L_ON : (a->dirty ? L_DIM : L_OFF);
    if (a->stepMode) {                               /* STEP: Oktavtasten zeigen, ob es links/rechts weitere Seiten gibt */
        btn[GB_OCTDN] = a->stepPage > 0 ? L_MID : L_OFF;
        btn[GB_OCTUP] = a->stepPage < pages - 1 ? L_MID : L_OFF;
    } else {                                         /* PLAY: Oktave wie die Werks-Firmware */
        const int o = a->octave, m = o < 0 ? -o : o;
        const uint8_t v = m == 0 ? L_OFF : m == 1 ? blink(now, 1000u, L_ON, L_OFF) : m == 2 ? blink(now, 300u, L_ON, L_OFF) : L_ON;
        btn[o < 0 ? GB_OCTDN : GB_OCTUP] = v;
        if (o == 0 && a->transpose) btn[a->transpose < 0 ? GB_OCTDN : GB_OCTUP] = L_DIM; /* nur Transpose aktiv */
    }
}
