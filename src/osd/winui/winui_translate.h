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

// translate one runtime TCHAR string (tree items, list columns, ...);
// returns a pointer owned by an internal cache, valid for the process
const TCHAR *winui_translate_tstring(const TCHAR *src);

#endif // WINUI_WINUI_TRANSLATE_H
