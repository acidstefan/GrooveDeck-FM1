/* GrooveDeck FM-1 - Bedienfeld am PC: Nachbildung des FM-1-Layouts mit Display, LEDs, Tasten, Buttons,
   Drehreglern und MASTER. Laeuft mit derselben Kern-Software wie die Firmware (gd_app), aber nativ am PC:
   voller Klang in Echtzeit, ohne Emulator. Zoom stufenlos.

   Bedienung (STEP-Modus: weisse Tasten = Steps, schwarze = Kanaele; OCT- + OCT+ halten + schwarze Taste = Mute)
     Maus      Tasten/Buttons anklicken (halten = gedrueckt halten), Regler: Mausrad darueber oder ziehen
     Tastatur  Klaviatur: weisse Tasten Z X C V B N M , . / Q W E R T Y, schwarze darueber (S D F H J L ; ' 3 4 6)
               F1 FX  F2 SEL  F3 ENV  F4 LFO  F5 EDIT  F6 GLO  F7 HOME  F8 SAVE  F9 ARP  F10 SEQ
               F11 oder Leertaste PLAY  F12 REC   [ OCT-   ] OCT+
               Pfeil hoch/runter = SELECT, Pfeil links/rechts = PRESETS
     Zoom      Strg + Mausrad, Strg + Plus/Minus, Strg + 0 = 100 %; Fenster frei ziehbar

   gd_panel [--zoom 1.0] [--test ausgabe.ppm]   (--test: ohne Fenster, klickt eine Folge und speichert ein Bild) */
#include "gd_panel_draw.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SR 44100

static int key_for_scancode(SDL_Scancode s)
{
    static const SDL_Scancode white[16] = {
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_B, SDL_SCANCODE_N,
        SDL_SCANCODE_M, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD, SDL_SCANCODE_SLASH,
        SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_E, SDL_SCANCODE_R, SDL_SCANCODE_T, SDL_SCANCODE_Y };
    static const SDL_Scancode black[11] = {
        SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_H, SDL_SCANCODE_J, SDL_SCANCODE_L,
        SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_3, SDL_SCANCODE_4, SDL_SCANCODE_6 };
    int i;
    for (i = 0; i < 16; ++i) if (s == white[i]) return find(C_KEY, gd_white_key(i));
    for (i = 0; i < 11; ++i) if (s == black[i]) return find(C_KEY, gd_black_key(i));
    switch (s) {
        case SDL_SCANCODE_F1: return find(C_BTN, GB_FX);
        case SDL_SCANCODE_F2: return find(C_BTN, GB_SEL);
        case SDL_SCANCODE_F3: return find(C_BTN, GB_ENV);
        case SDL_SCANCODE_F4: return find(C_BTN, GB_LFO);
        case SDL_SCANCODE_F5: return find(C_BTN, GB_EDIT);
        case SDL_SCANCODE_F6: return find(C_BTN, GB_GLO);
        case SDL_SCANCODE_F7: return find(C_BTN, GB_HOME);
        case SDL_SCANCODE_F8: return find(C_BTN, GB_SAVE);
        case SDL_SCANCODE_F9: return find(C_BTN, GB_ARP);
        case SDL_SCANCODE_F10: return find(C_BTN, GB_SEQ);
        case SDL_SCANCODE_F11: case SDL_SCANCODE_SPACE: return find(C_BTN, GB_PLAY);
        case SDL_SCANCODE_F12: return find(C_BTN, GB_REC);
        case SDL_SCANCODE_LEFTBRACKET: return find(C_BTN, GB_OCTDN);
        case SDL_SCANCODE_RIGHTBRACKET: return find(C_BTN, GB_OCTUP);
        default: return -1;
    }
}

/* ------------------------------------------------------------------ Audio */
static void audio_cb(void *ud, Uint8 *stream, int len)
{
    float *out = (float *)stream, l[64], r[64];
    int n = len / (int)(2 * sizeof(float)), done = 0, i;
    uint8_t mo[64];
    (void)ud;
    while (done < n) {
        const int c = n - done < 64 ? n - done : 64;
        gd_app_audio(&app, l, r, c);
        for (i = 0; i < c; ++i) {
            out[2 * (done + i)] = l[i] < -1.0f ? -1.0f : l[i] > 1.0f ? 1.0f : l[i];
            out[2 * (done + i) + 1] = r[i] < -1.0f ? -1.0f : r[i] > 1.0f ? 1.0f : r[i];
        }
        done += c;
    }
    while (gd_app_midi_out_read(&app, mo, sizeof mo) > 0) { } /* MIDI-Ausgang: am PC nicht verbunden */
}

static void save_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", PW, PH);
    for (i = 0; i < PW * PH; ++i) { fputc((px[i] >> 16) & 255, f); fputc((px[i] >> 8) & 255, f); fputc(px[i] & 255, f); }
    fclose(f);
}

/* --test: Ablauf ohne Fenster und ohne Ton; Bedienung wie per Maus, Bild am Ende */
static void run_audio(double secs)
{
    float l[64], r[64];
    long k, n = (long)(secs * SR / 64);
    uint8_t mo[64];
    for (k = 0; k < n; ++k) { gd_app_audio(&app, l, r, 64); gd_app_tick(&app, 1); while (gd_app_midi_out_read(&app, mo, 64) > 0) { } }
}
static void click(int i, double hold) { press(i, 1); run_audio(hold); press(i, 0); run_audio(0.05); }
static int test_run(const char *out)
{
    gd_app_init(&app, SR);
    gd_app_set_battery(&app, 72, 0);
    run_audio(0.2);
    click(find(C_KEY, gd_white_key(0)), 0.05);
    click(find(C_KEY, gd_white_key(4)), 0.05);
    click(find(C_KEY, gd_white_key(8)), 0.05);
    click(find(C_BTN, GB_PLAY), 0.05);
    turn(GE_K1, 3);
    set_master(0.7f);
    run_audio(1.3);
    draw_panel(app.audioMs);
    save_ppm(out);
    printf("Bild: %s\n", out);
    return 0;
}

int main(int argc, char **argv)
{
    float zoom = 1.0f;
    SDL_Window *win;
    SDL_Renderer *ren;
    SDL_Texture *tex;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID dev;
    int quit = 0, drag = -1, dragY = 0, i;
    int keyOf[SDL_NUM_SCANCODES];
    for (i = 0; i < SDL_NUM_SCANCODES; ++i) keyOf[i] = -1;
    layout();
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--test") && i + 1 < argc) return test_run(argv[i + 1]);
        if (!strcmp(argv[i], "--zoom") && i + 1 < argc) zoom = (float)atof(argv[++i]);
    }
    if (zoom < 0.3f) zoom = 0.3f;
    if (zoom > 3.0f) zoom = 3.0f;
    gd_app_init(&app, SR);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    win = SDL_CreateWindow("GrooveDeck FM-1", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           (int)(PW * zoom), (int)(PH * zoom), SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!ren) ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_RenderSetLogicalSize(ren, PW, PH);          /* Zoom = Fenstergroesse; Mauskoordinaten werden umgerechnet */
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, PW, PH);

    SDL_zero(want);
    want.freq = SR; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 512; want.callback = audio_cb;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) fprintf(stderr, "Kein Ton: %s (Bedienung geht trotzdem)\n", SDL_GetError());
    else SDL_PauseAudioDevice(dev, 0);

    while (!quit) {
        SDL_Event e;
        static uint32_t lastTick;
        const uint32_t now = SDL_GetTicks();
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: quit = 1; break;
                case SDL_MOUSEBUTTONDOWN:
                    if (e.button.button == SDL_BUTTON_LEFT) {
                        const int h = hit(e.button.x, e.button.y);
                        if (h < 0) break;
                        if (ctl[h].kind == C_KEY || ctl[h].kind == C_BTN) { SDL_LockAudioDevice(dev); press(h, 1); SDL_UnlockAudioDevice(dev); }
                        else { pressed[h] = 1; }
                        drag = h; dragY = e.button.y;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (e.button.button == SDL_BUTTON_LEFT && drag >= 0) {
                        if (ctl[drag].kind == C_KEY || ctl[drag].kind == C_BTN) { SDL_LockAudioDevice(dev); press(drag, 0); SDL_UnlockAudioDevice(dev); }
                        else pressed[drag] = 0;
                        drag = -1;
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (drag >= 0 && ctl[drag].kind == C_ENC) {
                        while (e.motion.y <= dragY - 12) { turn(ctl[drag].id, +1); dragY -= 12; }
                        while (e.motion.y >= dragY + 12) { turn(ctl[drag].id, -1); dragY += 12; }
                    } else if (drag >= 0 && ctl[drag].kind == C_MASTER) {
                        set_master(master - (float)(e.motion.y - dragY) / 300.0f);
                        dragY = e.motion.y;
                    }
                    break;
                case SDL_MOUSEWHEEL: {
                    int mx, my, lx, ly, h;
                    if (SDL_GetModState() & KMOD_CTRL) { zoom *= e.wheel.y > 0 ? 1.1f : 1.0f / 1.1f; goto rezoom; }
                    SDL_GetMouseState(&mx, &my);
                    {   /* Fenster- in Panel-Koordinaten */
                        int ww, wh;
                        float sx, sy;
                        SDL_GetWindowSize(win, &ww, &wh);
                        sx = (float)PW / (float)ww; sy = (float)PH / (float)wh;
                        if (sx < sy) sx = sy;
                        lx = (int)((mx - (ww - PW / sx) / 2) * sx); ly = (int)((my - (wh - PH / sx) / 2) * sx);
                    }
                    h = hit(lx, ly);
                    if (h >= 0 && ctl[h].kind == C_ENC) turn(ctl[h].id, e.wheel.y > 0 ? 1 : -1);
                    else if (h >= 0 && ctl[h].kind == C_MASTER) set_master(master + (e.wheel.y > 0 ? 0.02f : -0.02f));
                    break;
                }
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    const int down = e.type == SDL_KEYDOWN;
                    const SDL_Scancode s = e.key.keysym.scancode;
                    if (down && (e.key.keysym.mod & KMOD_CTRL)) {
                        if (s == SDL_SCANCODE_EQUALS || s == SDL_SCANCODE_KP_PLUS || s == SDL_SCANCODE_RIGHTBRACKET) { zoom *= 1.1f; goto rezoom; }
                        if (s == SDL_SCANCODE_MINUS || s == SDL_SCANCODE_KP_MINUS || s == SDL_SCANCODE_SLASH) { zoom /= 1.1f; goto rezoom; }
                        if (s == SDL_SCANCODE_0 || s == SDL_SCANCODE_KP_0) { zoom = 1.0f; goto rezoom; }
                        break;
                    }
                    if (e.key.repeat) break;
                    if (down && s == SDL_SCANCODE_UP) { turn(GE_SELECT, 1); break; }
                    if (down && s == SDL_SCANCODE_DOWN) { turn(GE_SELECT, -1); break; }
                    if (down && s == SDL_SCANCODE_RIGHT) { turn(GE_PRESETS, 1); break; }
                    if (down && s == SDL_SCANCODE_LEFT) { turn(GE_PRESETS, -1); break; }
                    if (down) keyOf[s] = key_for_scancode(s);
                    if (keyOf[s] >= 0) { SDL_LockAudioDevice(dev); press(keyOf[s], down); SDL_UnlockAudioDevice(dev); }
                    if (!down) keyOf[s] = -1;
                    break;
                }
                default: break;
            }
            continue;
        rezoom:
            if (zoom < 0.3f) zoom = 0.3f;
            if (zoom > 3.0f) zoom = 3.0f;
            SDL_SetWindowSize(win, (int)(PW * zoom), (int)(PH * zoom));
        }
        if (!lastTick) lastTick = now;
        gd_app_tick(&app, (int)(now - lastTick));
        lastTick = now;
        draw_panel(app.audioMs);
        SDL_UpdateTexture(tex, NULL, px, PW * 4);
        SDL_SetRenderDrawColor(ren, 20, 21, 25, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);
        SDL_Delay(10);
    }
    if (dev) SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
