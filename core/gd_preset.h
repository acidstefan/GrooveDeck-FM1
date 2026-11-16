/* GrooveDeck FM-1 - Werkspresets (440 in 22 Richtungen x 5 Untergruppen) und Kanal-Presets. */
#ifndef GD_PRESET_H
#define GD_PRESET_H
#include <stdint.h>
#include "gd_seq.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { uint8_t genre, sub; const char *name; uint32_t offset; } gd_preset_entry;
typedef struct { uint8_t part; const char *name; uint32_t offset; } gd_chan_entry;

extern const int gd_num_genres;
extern const char *const gd_genre_names[];
extern const char *const gd_sub_names[][5];
extern const int gd_num_presets;
extern const gd_preset_entry gd_presets[];
extern const int gd_num_chan_presets;
extern const gd_chan_entry gd_chan_presets[];
extern const unsigned gd_preset_blob_size;
extern const uint8_t gd_preset_blob[];

/* Werkspreset laden: Patch zuruecksetzen (Master-Lautstaerke bleibt), Pattern und Parameter setzen. 0 = ok */
int gd_preset_load(gd_seq *s, int index);
/* Stand als Datenblock sichern (gleiches Format wie die Werkspresets). Rueckgabe: Laenge oder -1 */
int gd_preset_save(const gd_seq *s, uint8_t *buf, int cap);
int gd_preset_load_data(gd_seq *s, const uint8_t *data, int len);
/* Kanal-Preset auf einen Part anwenden (Karten Quelle/Ton/Filter/Amp/Mod, Level/Pan/FX bleiben). 0 = ok */
int gd_chan_preset_apply(gd_seq *s, int part, int index);
/* Kanal-Presets fuer einen Part: Anzahl bzw. globaler Index des n-ten Eintrags (-1 wenn keiner) */
int gd_chan_preset_count(int part);
int gd_chan_preset_nth(int part, int n);
/* Part, dessen Kanal-Presets gelten (Drum7B nutzt die Becken von Drum7A, Lead die Bass-Liste und umgekehrt nicht) */
int gd_chan_preset_cat(int part);

#ifdef __cplusplus
}
#endif
#endif
