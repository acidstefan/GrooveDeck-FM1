/* GrooveDeck FM-1 - Step-Sequencer (16-64 Steps), Keyboard-Routing, Arpeggiator/Chord-Laufzeit.
   Entspricht dem Sequencer-Teil von GrooveDeckProcessor::processBlock / kbdIn / processArp. */
#ifndef GD_SEQ_H
#define GD_SEQ_H
#include <stdint.h>
#include "gd_dsp.h"
#include "gd_arp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t stepMask[GD_PARTS];
    uint8_t kbdNote[2][GD_MAX_STEPS];
    uint8_t kbdLen[2][GD_MAX_STEPS];
    int patLen;
} gd_pattern;

typedef struct { int part, note; float offBeat; } gd_pending;
typedef struct { int note; float offBeat; } gd_arp_off;

typedef struct {
    uint8_t phys[128], route[128], cnN[128];
    uint8_t cn[128][GA_MAXCHORD];
    int held[16], nHeld, down, lastVel, wasOn;
    uint32_t rng;
    gd_arp_off offs[64];
    int nOffs;
    int16_t user[GA_USER_STYLES][GA_STEPS];
} gd_arp_rt;

typedef struct {
    gd_engine *eng;
    gd_pattern pat;
    int playing, wasPlaying, seqOn, currentStep;
    float beat, arpFree;
    uint16_t muteMask;
    gd_pending pending[96];
    int nPending;
    gd_arp_rt arp[2];
} gd_seq;

void gd_pattern_clear(gd_pattern *p);
void gd_seq_init(gd_seq *s, gd_engine *e);
void gd_seq_set_playing(gd_seq *s, int on);
/* Live-Noten (Tastatur, MIDI-In): Keyboard-Parts laufen ueber Arp/Chord, Drums direkt */
void gd_seq_key(gd_seq *s, int part, int note, int vel, int on);
void gd_seq_panic(gd_seq *s);
/* Sequencer + Engine fuer n Samples; addiert auf l/r */
void gd_seq_render(gd_seq *s, float *l, float *r, int n);
void gd_seq_read_arp(const gd_seq *s, int part, gd_arp_params *P);
/* Einzelschritte (fuer die Aufteilung auf zwei Kerne): Ereignisse im Beat-Fenster [b0, b1), Laufzeitbegrenzung */
void gd_seq_tick(gd_seq *s, float b0, float b1);
void gd_seq_wrap(gd_seq *s);

#ifdef __cplusplus
}
#endif
#endif
