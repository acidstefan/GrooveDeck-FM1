/* GrooveDeck FM-1 - Reglerseiten: welche Parameter die Knoepfe 1-4 auf welcher Seite bedienen. */
#ifndef GD_UI_PARAMS_H
#define GD_UI_PARAMS_H
#include "gd_app.h"

enum { SC_NONE = 0, SC_GLOBAL, SC_PART, SC_PATLEN, SC_CHAN, SC_MUTE };
enum { PK_CONT = 0, PK_CHOICE, PK_SWITCH, PK_INT, PK_TEMPO, PK_STYLE, PK_FORM, PK_GRID, PK_VOICING, PK_CHAN };

typedef struct {
    const char *label;
    uint8_t scope, kind;
    int16_t field;
    float lo, hi;
    const char *const *names; /* PK_CHOICE: Namen je Wert (Index = Wert) */
} gd_pdesc;

const gd_pdesc *gd_page_param(int page, int sub, int part, int knob);
const char *gd_page_name(int page);
const char *gd_part_short(int part);
float gd_param_value(const gd_app *a, const gd_pdesc *d);
void gd_param_format(const gd_app *a, const gd_pdesc *d, char *buf, int cap);
float gd_param_norm(const gd_app *a, const gd_pdesc *d); /* 0..1 fuer den Balken */
#endif
