// license:BSD-3-Clause
/***************************************************************************

    winui_translate.h

    MAMEUI GUI self-translation (MAMEPlus port).

    Uses a dedicated gettext dictionary: language/<lang>/winui.mo holds
    the "winui" message context entries, kept separate from the official
    strings.mo so the upstream translation files stay untouched. GUI
    strings (menus, dialogs) are looked up by their English source text
    at runtime.

***************************************************************************/

#ifndef WINUI_WINUI_TRANSLATE_H
#define WINUI_WINUI_TRANSLATE_H

#include "windows.h"

#include <string>

// resolve a language option value (long official name) to the short
// directory name MAMEPlus used for its .mmo dictionaries; falls back to
// the normalized name itself when there is no mapping
std::string winui_plus_lang_shortname(const std::string &language);

// resolve a language option value to the official long directory name
// (the low-priority fallback location for dictionaries)
std::string winui_plus_lang_longname(const std::string &language);

// command IDs for the runtime-inserted Options > Language menu
#define ID_LANGUAGE_MENU   41000
#define ID_LANGUAGE_FIRST  41001
#define ID_LANGUAGE_LAST   41099

// apply a language change: save the option, reload both the internal
// UI dictionary and the GUI dictionary, and rebuild the main menu
// (implemented in winui.cpp)
void winui_apply_language(const std::string &lang);

// insert/refresh the Language popup at the end of the Options menu
bool winui_insert_language_menu(HMENU hMenuBar);

// handle a WM_COMMAND from the Language menu; returns true if handled
bool winui_handle_language_command(int id);

// load the translation dictionary from the global options (GUI phase);
// mirrors winui.cpp load_translation(), including the Chinese default
void winui_init_translation();

// reload the dictionary after a language change and retranslate the
// main menu (dialogs are handled by the CBT hook on activation)
void winui_reload_translation();

// install a CBT hook that translates every dialog when it becomes active
void winui_install_translate_hook();

// recursively translate a menu (by source-text lookup)
void winui_translate_menu(HMENU hMenu);

// translate a top-level window caption and all child controls
void winui_translate_window(HWND hwnd);

// translate one runtime UTF-8 string in a context, caching results
// (game titles from lst.mmo use the "lst" context); returns the
// original text when no translation exists
std::string winui_translate_utf8(const char *src, const char *context);

// translate one runtime TCHAR string (tree items, list columns, ...);
// returns a pointer owned by an internal cache, valid for the process
const TCHAR *winui_translate_tstring(const TCHAR *src);

#endif // WINUI_WINUI_TRANSLATE_H
