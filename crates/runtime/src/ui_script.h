#ifndef SCUZZ_UI_SCRIPT_H
#define SCUZZ_UI_SCRIPT_H

#include "scuzz_ui.h"

/* Hit-scan over the session root (dump [taps]/[scrolls] and script engine). */
int sz_ui_collect_buttons(SzUiSession *session, SzView **buttons, int cap);
int sz_ui_collect_scrolls(SzUiSession *session, SzView **scrolls, int cap);

/* SCUZZ_UI_SCRIPT playback: text buffer (inject), file (replay), or one
 * env-driven tap. */
void sz_ui_script_play_text(SzUiSession *session, char *text);
void sz_ui_script_run_file(SzUiSession *session, const char *path);
void sz_ui_scripted_button_tap(SzUiSession *session, int prefer_upper);

#endif
