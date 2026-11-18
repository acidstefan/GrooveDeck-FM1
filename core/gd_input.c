/* GrooveDeck FM-1 - Tastenmatrix, Buttons, Drehgeber. */
#include "gd_input.h"
#include <string.h>

/* Slot -> Matrixzelle: oberes Nibble = Spalte (Index in rows[]), unteres = Rueckleitung.
   Slots 0..13 Buttons, 14..40 Klaviatur von links nach rechts (Reihenfolge der Werks-Firmware). */
static const uint8_t k_cells[41] = {
    0x14, 0x24, 0x71, 0x51, 0x31, 0x11, 0xa2, 0x92, 0x81, 0x61, 0x41, 0x21, 0x02, 0x82,
    0x44, 0x34, 0x64, 0x54, 0x84, 0x74, 0x94, 0xa4, 0x04, 0x13, 0x23, 0x33, 0x43, 0x53,
    0x63, 0x73, 0x83, 0x93, 0xa3, 0x03, 0x12, 0x22, 0x32, 0x42, 0x52, 0x72, 0x62
};
/* Buttons in Slot-Reihenfolge */
static const uint8_t k_btn[14] = { GB_OCTDN, GB_OCTUP, GB_FX, GB_SEL, GB_ENV, GB_LFO, GB_EDIT, GB_GLO, GB_HOME, GB_SAVE,
                                   GB_ARP, GB_SEQ, GB_PLAY, GB_REC };
/* Drehgeber: Zelle Phase A, Zelle Phase B (Reihenfolge wie GE_*) */
static const uint8_t k_enc[GE_COUNT][2] = {
    { 0x10, 0x20 }, /* SELECT */
    { 0x15, 0x25 }, /* PRESETS */
    { 0x30, 0x40 }, /* ALGORITHM */
    { 0x91, 0xa1 }, /* KNOB 1 */
    { 0x90, 0xa0 }, /* KNOB 2 */
    { 0x70, 0x80 }, /* KNOB 3 */
    { 0x50, 0x60 }  /* KNOB 4 */
};

static int closed(const uint8_t rows[GD_SCAN_ROWS], uint8_t cell)
{
    const unsigned col = cell >> 4, bit = cell & 15u;
    return col < GD_SCAN_ROWS && !(rows[col] & (1u << bit));
}

void gd_input_init(gd_input *in)
{
    memset(in, 0, sizeof *in);
    in->master = 0xFFFF;
}

void gd_input_scan(gd_input *in, const uint8_t rows[GD_SCAN_ROWS], gd_app *app)
{
    uint64_t now = 0, diff;
    int i;
    for (i = 0; i < 41; ++i) if (closed(rows, k_cells[i])) now |= (uint64_t)1 << i;
    /* Entprellen: zwei gleiche Abtastungen hintereinander (wie die Werks-Firmware) */
    if (now != in->cand) { in->cand = now; in->candCount = 1; }
    else if (in->candCount < 2) ++in->candCount;
    if (in->candCount >= 2 && in->cand != in->stable) {
        diff = in->cand ^ in->stable;
        for (i = 0; i < 41; ++i) {
            if (!((diff >> i) & 1u)) continue;
            {
                const int down = (int)((in->cand >> i) & 1u);
                if (i < 14) gd_app_post_button(app, k_btn[i], down);
                else gd_app_post_key(app, i - 14, down);
            }
        }
        in->stable = in->cand;
    }
    /* Drehgeber: Rastung im Uhrzeigersinn durchlaeuft 00, 01, 11, 10, 00 (B schliesst zuerst) */
    for (i = 0; i < GE_COUNT; ++i) {
        static const int8_t q[4][4] = { /* [alt][neu], Zustand = A<<1 | B */
            { 0, +1, -1, 0 }, { -1, 0, 0, +1 }, { +1, 0, 0, -1 }, { 0, -1, +1, 0 } };
        const uint8_t s = (uint8_t)((closed(rows, k_enc[i][0]) << 1) | closed(rows, k_enc[i][1]));
        if (s == in->encState[i]) continue;
        in->encAcc[i] = (int8_t)(in->encAcc[i] + q[in->encState[i]][s]);
        in->encState[i] = s;
        if (s == 0) {
            if (in->encAcc[i] >= 2) gd_app_post_encoder(app, i, +1);
            else if (in->encAcc[i] <= -2) gd_app_post_encoder(app, i, -1);
            in->encAcc[i] = 0;
        }
    }
}

void gd_input_master(gd_input *in, int raw, gd_app *app)
{
    int v;
    if (raw < 0) raw = 0;
    if (raw > 1023) raw = 1023;
    v = raw * 1000 / 1023;
    if (in->master != 0xFFFF && (v > in->master ? v - in->master : in->master - v) < 4) return; /* Rauschen */
    in->master = (uint16_t)v;
    gd_app_post_master(app, (float)v / 1000.0f);
}

static void put(uint8_t grid[GD_LED_COLS][GD_LED_LINES], uint8_t cell, uint8_t v)
{
    /* rows[]-Index = 595-Spalte + 1 (Abtast-Pipeline: rows[0] gehoert zu Spalte 10) */
    const int col = ((cell >> 4) + GD_LED_COLS - 1) % GD_LED_COLS, line = (cell & 15) - 1;
    if (col >= 0 && col < GD_LED_COLS && line >= 0 && line < GD_LED_LINES) grid[col][line] = v;
}

void gd_input_led_grid(const uint8_t keys[GD_KEYS], const uint8_t btn[GB_COUNT], uint8_t playGreen,
                       uint8_t grid[GD_LED_COLS][GD_LED_LINES])
{
    int i;
    memset(grid, 0, GD_LED_COLS * GD_LED_LINES);
    for (i = 0; i < 14; ++i) put(grid, k_cells[i], btn[k_btn[i]]);
    for (i = 0; i < GD_KEYS; ++i) put(grid, k_cells[14 + i], keys[i]);
    put(grid, 0x91, playGreen);   /* gruene PLAY-LED: eigene Leitung (Spalte 8, Zeile 1) */
}
