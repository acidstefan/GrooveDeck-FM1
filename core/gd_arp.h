/* GrooveDeck FM-1 - Arpeggiator und Chord Variator (Portierung von GrooveDeck Source/Arp.h). */
#ifndef GD_ARP_H
#define GD_ARP_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { GA_STYLES = 24, GA_FACTORY = 16, GA_USER_STYLES = 8, GA_STEPS = 16, GA_SLOTS = 8, GA_MAXPOOL = 64, GA_MAXCHORD = 8,
       GA_FORMS = 32, GA_USER_FORMS = 8, GA_FORM_CHOICES = GA_FORMS + GA_USER_FORMS, GA_VOICINGS = 5 };

typedef struct {
    int arpOn, hold, chordOn;
    int style, grid, octs, voicing, slotLen;
    float gate, shuffle, accent;
    int slots[GA_SLOTS];
    int user[GA_STEPS];
} gd_arp_params;

typedef struct { int n; int note[GA_MAXPOOL]; } gd_arp_pool;
typedef struct { int n; int notes[GA_MAXCHORD]; int acc; } gd_arp_out;

const char *gd_arp_form_name(int i);
const int *gd_arp_form_slots(int i); /* nur Werksformen 0..31 */
const char *gd_arp_style_name(int i);
const char *gd_arp_voicing_name(int i);
const char *gd_arp_grid_name(int i);
float gd_arp_grid_beats(int i); /* in Vierteln */
int gd_arp_style_code(int style, int step);
int gd_arp_chord_notes(const gd_arp_params *P, int key, int *out, int inSlotOrder);
void gd_arp_build_pool(const gd_arp_params *P, const int *held, int nHeld, gd_arp_pool *pool, int sorted);
gd_arp_out gd_arp_pick(long k, const gd_arp_pool *pool, const gd_arp_params *P, uint32_t *rng);

#ifdef __cplusplus
}
#endif
#endif
