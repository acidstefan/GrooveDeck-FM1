/* GrooveDeck FM-1 - Bedienfeld (gemeinsamer Teil von gd_panel und web/gd_web.c) */
#ifndef GD_PANEL_DRAW_H
#define GD_PANEL_DRAW_H
#include "gd_app.h"
#include "gd_input.h"

#define PW 1500
#define PH 900
#ifndef GD_PANEL_HELP
#define GD_PANEL_HELP "Maus: klicken/halten, Regler: Rad oder ziehen   Strg+Rad / Strg +/-: Zoom   Hilfe: siehe Kopf von host/gd_panel.c"
#endif

typedef struct { int x, y, w, h; } rect;
enum { C_KEY, C_BTN, C_ENC, C_MASTER };
typedef struct { int kind, id; rect r; const char *label; } control;

extern gd_app app;
extern uint32_t px[PH * PW];
extern control ctl[64];
extern int nctl, pressed[64];
extern float master;

void layout(void);
void draw_panel(uint32_t nowMs);
void press(int i, int down);         /* Taste/Button eines Controls */
void turn(int enc, int delta);
void set_master(float v);
int hit(int x, int y);               /* Control unter Panel-Koordinaten oder -1 */
int find(int kind, int id);
#endif
