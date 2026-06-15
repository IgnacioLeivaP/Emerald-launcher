#pragma once
#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif
