/* GrooveDeck FM-1 - Groessen, die vom Ziel abhaengen. Ueber -D beim Compilieren ueberschreibbar. */
#ifndef GD_CONFIG_H
#define GD_CONFIG_H

/* Stimmen je Keyboard-Part (Bass/Lead). Plugin: 6. Auf dem FM-1 kostet jede Stimme Rechenzeit. */
#ifndef GD_MAX_VOICES
#define GD_MAX_VOICES 6
#endif

/* Delay-Speicher in Samples je Kanal (16 Bit). Plugin: 2,5 s. Auf dem FM-1 ist der Speicher knapp (512 KB RAM):
   32768 = 0,74 s bei 44,1 kHz = 128 KB fuer beide Kanaele; laengere Delays werden dann auf 0,74 s begrenzt
   (betrifft nur sehr langsame Tempi mit langen Notenwerten, z. B. Dub-Presets). */
#ifndef GD_DELAY_LEN
#ifdef GD_TARGET_FM1
#define GD_DELAY_LEN 32768
#else
#define GD_DELAY_LEN 110256
#endif
#endif

/* Hall: 1 = Stereo-Kammfilter wie im Plugin (klanggleich), 0 = Kammfilter mono (spart ca. 40 % der Hall-Rechenzeit) */
#ifndef GD_REVERB_STEREO
#define GD_REVERB_STEREO 1
#endif
/* Hall-Speicher in Samples (float). 44,1 kHz: stereo 25.450 (100 KB), mono 14.300 (57 KB). Bei hoeheren
   Abtastraten werden die Laengen bei Bedarf auf diese Groesse gestaucht. */
#ifndef GD_REVERB_LEN
#if GD_REVERB_STEREO
#define GD_REVERB_LEN 25500
#else
#define GD_REVERB_LEN 14300
#endif
#endif

/* Soundgoodizer-Analyse mit voller Abtastrate (wie im Plugin). 0 = halbe Rate: spart Rechenzeit, Klangbalance weicht dann leicht ab */
#ifndef GD_GOOD_FULLRATE
#define GD_GOOD_FULLRATE 1
#endif

/* Laengster Pattern (Steps) */
#define GD_MAX_STEPS 64

#endif
