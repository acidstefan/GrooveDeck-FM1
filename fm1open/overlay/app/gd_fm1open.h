/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * GrooveDeck FM-1 auf FM1 Open: Verbindung zwischen den App-Dateien (screen.c, midi_router.c, project.c,
 * board/audio.c, board/panel.c) und dem GrooveDeck-Kern (app/gd). */
#ifndef GD_FM1OPEN_H
#define GD_FM1OPEN_H
#include <stdint.h>
#include <stddef.h>
#include "fm1.h"
#include "gd/gd_app.h"

extern gd_app gd_state;                  /* der ganze GrooveDeck-Zustand (screen.c) */
extern volatile int gd_ready;            /* 1, sobald gd_state initialisiert ist */

/* Audio-Interrupt: einen fertigen Block (FM1_BLOCK_FRAMES Stereo, Q23) abholen. 0 = ok. Liegt im RAM. */
FM1_AUDIO_CODE int gd_render_block(int32_t *out);
extern volatile uint32_t gd_underruns;

/* board/panel.c */
void gd_led_level(unsigned key, unsigned level);
void gd_led_green(unsigned level);

/* midi_router.c: eingegangene MIDI-Bytes an den Kern geben, ausgehende senden (Control-Task) */
void gd_midi_poll(void);

/* project.c: Benutzerstand im Flash (Control-Task) */
void gd_flash_load(void);
int gd_flash_save_hook(const uint8_t *data, int len);   /* aus dem Audio-Interrupt: nur vormerken */
void gd_flash_poll(void);

/* gd_presetpack.c: Preset-Paket im Nutzerspeicher, Aufspielen per SysEx */
void gd_pack_init(void);                                  /* Control-Task, nach gd_flash_load */
void gd_pack_sysex(const uint8_t *message, size_t bytes); /* Routing-Task: nur kopieren */
void gd_pack_poll(void);                                  /* Control-Task */

#endif
