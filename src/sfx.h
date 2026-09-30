#pragma once
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool sfx_init(void);
void sfx_shutdown(void);
void sfx_suspend(void);   /* free the audio device (game takes over) */
void sfx_resume(void);    /* reopen it on return to the launcher */

void sfx_play_nav(void);              /* updown.wav        — navigate up/down in any menu  */
void sfx_play_confirm(void);          /* enter.wav         — confirm / open submenu        */
void sfx_play_back(void);             /* backinmenu.wav    — go back inside a menu         */
void sfx_play_boot(void);             /* boot.wav          — app start                     */
void sfx_play_enter_game(void);       /* entergame.wav     — a game is about to launch     */
void sfx_play_open_menu(void);        /* openmenuingame.wav — overlay opened while playing */
void sfx_play_back_to_launcher(void); /* backtolauncher.wav — confirmed return to launcher */
void sfx_play_next_week(void);        /* youcangotonextweek.wav — AST week complete        */

/* While a game runs the menu audio device is closed (the game has its own);
   sounds triggered then are mixed into the game's 48 kHz stereo output. */
void sfx_mix_game(short *stereo, size_t frames);

/* Menu music (branding.json "assets.music"): a looping .ogg or .wav under
   the menus, faded in and out, silent during games. */
bool sfx_music_load(const char *path);
bool sfx_music_loaded(void);
void sfx_music_play(bool on);
void sfx_music_set_volume(float volume);   /* 0..1 */

#ifdef __cplusplus
}
#endif
