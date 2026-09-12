#ifndef SCUZZ_UI_SCRIPT_H
#define SCUZZ_UI_SCRIPT_H

#include "scuzz_ui.h"

/* A11y-preorder collect over the session root (dump [taps]/[scrolls] and script). */
int sz_ui_collect_buttons(SzUiSession *session, SzView **buttons, int cap);
int sz_ui_collect_scrolls(SzUiSession *session, SzView **scrolls, int cap);
int sz_ui_scroll_index(SzUiSession *session, int index, float dy);

/* SCUZZ_UI_SCRIPT playback (inject document replay) and one env-driven tap.
 * The inject schema v=1 (`{"v":1,"kind":"inject","events":[...]}`) is the
 * only script format. A script path must end in `.json`. */
void sz_ui_script_play_json(SzUiSession *session, const char *text);
void sz_ui_script_run_file(SzUiSession *session, const char *path);
void sz_ui_scripted_button_tap(SzUiSession *session, int prefer_upper);

/* IO-only drive script: run every drive event of an inject document. */
void sz_script_run_drive_doc(const char *text);

/* Inject event field access (shared by the UI player and the drive runner). */
SzAdt *sz_jev_key(SzAdt *obj, const char *key);
int sz_jev_has(SzAdt *obj, const char *key);
int64_t sz_jev_int(SzAdt *obj, const char *key, int64_t d);
double sz_jev_num(SzAdt *obj, const char *key, double d);
const char *sz_jev_str(SzAdt *obj, const char *key);
int sz_jev_bool(SzAdt *obj, const char *key);

/* Run one `{"op":"drive",...}` event. */
void sz_script_drive_json(SzAdt *ev);

#endif
