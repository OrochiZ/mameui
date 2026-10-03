// license:BSD-3-Clause
/***************************************************************************

    winui_translate.cpp

    MAMEUI GUI self-translation (MAMEPlus port).

    Instead of porting Plus' custom .mmo framework, this rides on the
    official gettext approach, but with a dedicated dictionary file
    language/<lang>/winui.mo so the official strings.mo files stay
    untouched for upstream maintenance. The mo parser is a trimmed copy
    of util/language.cpp holding its own map (the official one is a
    process-wide singleton built around strings.mo).

***************************************************************************/

#include "winui_translate.h"

// standard windows headers
#include <tchar.h>

// MAME/MAMEUI headers
#include "emu.h"
#include "emuopts.h"
#include "emu_opts.h"
#include "resource.h"
#include "winutf8.h"
#include "winui.h"
#include "strconv.h"

#include "util/corestr.h"
#include "util/ioprocs.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>


//============================================================
//  LOCAL VARIABLES
//============================================================

static bool s_loaded = false;
static HHOOK s_cbt_hook = NULL;
static std::unordered_map<std::string, std::string> s_winui_map;
static std::unordered_map<std::wstring, std::wstring> s_tstring_cache;
static std::unordered_map<std::string, std::string> s_utf8_cache;

// languages listed in the Options > Language menu, parallel to the
// command IDs (empty first entry = the default)
static std::vector<std::string> s_menu_langs;

constexpr std::uint32_t MO_MAGIC = 0x950412de;
constexpr std::uint32_t MO_MAGIC_REVERSED = 0xde120495;


//============================================================
//  load_winui_mo - parse a compiled gettext .mo file into the
//  GUI dictionary (trimmed from util/language.cpp)
//============================================================

static void load_winui_mo(util::random_read &file)
{
	std::uint64_t size = 0;
	if (file.length(size) || (20 > size))
	{
		osd_printf_verbose("Error reading GUI translation file: %u-byte file is too small\n", size);
		return;
	}

	std::unique_ptr<std::uint32_t []> data_buf(new (std::nothrow) std::uint32_t [(size + 3) / 4]);
	if (!data_buf)
		return;

	auto const [err, actual] = util::read(file, data_buf.get(), size);
	if (err || (actual != size))
	{
		osd_printf_verbose("Error reading GUI translation file: requested %u bytes but got %u bytes\n", size, actual);
		return;
	}

	if ((data_buf[0] != MO_MAGIC) && (data_buf[0] != MO_MAGIC_REVERSED))
	{
		osd_printf_verbose("Error reading GUI translation file: unrecognized magic number 0x%08X\n", data_buf[0]);
		return;
	}

	auto const fetch_word =
			[reversed = data_buf[0] == MO_MAGIC_REVERSED, words = data_buf.get()] (size_t offset)
			{
				return reversed ? swapendian_int32(words[offset]) : words[offset];
			};

	std::uint32_t const number_of_strings = fetch_word(2);
	std::uint32_t const original_table_offset = fetch_word(3) >> 2;
	std::uint32_t const translation_table_offset = fetch_word(4) >> 2;
	if ((4 * (original_table_offset + (std::uint64_t(number_of_strings) * 2))) > size)
		return;
	if ((4 * (translation_table_offset + (std::uint64_t(number_of_strings) * 2))) > size)
		return;

	char const *const data = reinterpret_cast<char const *>(data_buf.get());
	for (std::uint32_t i = 1; number_of_strings > i; ++i)
	{
		std::uint32_t const original_length = fetch_word(original_table_offset + (2 * i));
		std::uint32_t const original_offset = fetch_word(original_table_offset + (2 * i) + 1);
		std::uint32_t const translation_length = fetch_word(translation_table_offset + (2 * i));
		std::uint32_t const translation_offset = fetch_word(translation_table_offset + (2 * i) + 1);
		if (((original_length + original_offset) >= size) || ((translation_length + translation_offset) >= size))
			continue;
		if (data[original_length + original_offset] || data[translation_length + translation_offset])
			continue;

		// the official strings.mo keeps the table views alive; we copy
		// instead so the buffer can go away immediately
		s_winui_map.emplace(
				std::string(&data[original_offset], original_length),
				std::string(&data[translation_offset], translation_length));
	}

	osd_printf_verbose("Loaded %u GUI translated strings from file\n", s_winui_map.size());
}


//============================================================
//  winui_plus_lang_shortname - map the official long language
//  name to the short directory name MAMEPlus used for its
//  .mmo dictionaries
//============================================================

std::string winui_plus_lang_shortname(const std::string &language)
{
	std::string name = language;
	if (name.empty() || name == "auto")
		name = "Chinese_Simplified";
	strreplace(name, " ", "_");
	strreplace(name, "(", "");
	strreplace(name, ")", "");

	static const std::pair<const char *, const char *> table[] =
	{
		{ "Chinese_Simplified",  "zh_CN" },
		{ "Chinese_Traditional", "zh_TW" },
		{ "Japanese",            "ja_JP" },
		{ "Korean",              "ko_KR" },
		{ "French",              "fr_FR" },
		{ "German",              "de_DE" },
		{ "Italian",             "it_IT" },
		{ "Spanish",             "es_ES" },
		{ "Catalan",             "ca_ES" },
		{ "Valencian",           "va_ES" },
		{ "Polish",              "pl_PL" },
		{ "Portuguese_Portugal", "pt_PT" },
		{ "Portuguese_Brazil",   "pt_BR" },
		{ "Hungarian",           "hu_HU" },
	};
	for (auto const &entry : table)
		if (!core_stricmp(name.c_str(), entry.first))
			return entry.second;
	return name;
}


//============================================================
//  winui_plus_lang_longname - map the MAMEPlus short directory
//  name back to the official long language directory name
//  (the low-priority fallback location)
//============================================================

std::string winui_plus_lang_longname(const std::string &language)
{
	std::string name = language;
	if (name.empty() || name == "auto")
		name = "Chinese_Simplified";
	strreplace(name, " ", "_");
	strreplace(name, "(", "");
	strreplace(name, ")", "");

	static const std::pair<const char *, const char *> table[] =
	{
		{ "zh_CN", "Chinese_Simplified" },
		{ "zh_TW", "Chinese_Traditional" },
		{ "ja_JP", "Japanese" },
		{ "ko_KR", "Korean" },
		{ "fr_FR", "French" },
		{ "de_DE", "German" },
		{ "it_IT", "Italian" },
		{ "es_ES", "Spanish" },
		{ "ca_ES", "Catalan" },
		{ "va_ES", "Valencian" },
		{ "pl_PL", "Polish" },
		{ "pt_PT", "Portuguese_Portugal" },
		{ "pt_BR", "Portuguese_Brazil" },
		{ "hu_HU", "Hungarian" },
	};
	for (auto const &entry : table)
		if (!core_stricmp(name.c_str(), entry.first))
			return entry.second;
	return name;
}


//============================================================
//  load_mmo_file - parse a legacy MAMEPlus .mmo dictionary and
//  merge its wide-string entries (the winui side of the file)
//  into the GUI dictionary under the given context, overriding
//  earlier entries
//============================================================

static void load_mmo_file(util::random_read &file, const char *context)
{
	std::uint64_t size = 0;
	if (file.length(size) || (16 > size))
	{
		osd_printf_verbose("Error reading legacy translation file: %u-byte file is too small\n", size);
		return;
	}

	std::unique_ptr<std::uint32_t []> data_buf(new (std::nothrow) std::uint32_t [(size + 3) / 4]);
	if (!data_buf)
		return;

	auto const [err, actual] = util::read(file, data_buf.get(), size);
	if (err || (actual != size))
	{
		osd_printf_verbose("Error reading legacy translation file: requested %u bytes but got %u bytes\n", size, actual);
		return;
	}

	if (data_buf[0] || (data_buf[1] != 3) || !data_buf[2])
	{
		osd_printf_verbose("Error reading legacy translation file: unrecognized header (placeholder %u, version %u)\n", data_buf[0], data_buf[1]);
		return;
	}

	std::uint32_t const number_of_strings = data_buf[2];
	std::uint64_t const string_base = 12 + (std::uint64_t(number_of_strings) * 16) + 4;
	if (string_base > size)
		return;
	std::uint32_t const str_size = data_buf[(string_base - 4) / 4];
	if ((string_base + str_size) > size)
		return;

	char const *const strings = reinterpret_cast<char const *>(data_buf.get()) + string_base;
	std::size_t merged = 0;
	for (std::uint32_t i = 0; number_of_strings > i; ++i)
	{
		// index entry: offsets of the UTF-8 pair, then the UTF-16LE pair
		std::uint32_t const wid_offset = data_buf[3 + (4 * i) + 2];
		std::uint32_t const wstr_offset = data_buf[3 + (4 * i) + 3];
		if ((wid_offset >= str_size) || (wstr_offset >= str_size))
			continue;

		char const *const wid = strings + wid_offset;
		char const *const wstr = strings + wstr_offset;
		std::size_t wid_bytes = 0;
		while (((wid_offset + wid_bytes) < str_size) && (wid[wid_bytes] || wid[wid_bytes + 1]))
			wid_bytes += 2;
		std::size_t wstr_bytes = 0;
		while (((wstr_offset + wstr_bytes) < str_size) && (wstr[wstr_bytes] || wstr[wstr_bytes + 1]))
			wstr_bytes += 2;
		if (!wid_bytes || !wstr_bytes)
			continue;

		std::wstring const wid_wide(reinterpret_cast<LPCWSTR>(wid), wid_bytes / 2);
		std::wstring const wstr_wide(reinterpret_cast<LPCWSTR>(wstr), wstr_bytes / 2);
		char *const key_utf8 = ui_utf8_from_wstring(wid_wide.c_str());
		char *const val_utf8 = ui_utf8_from_wstring(wstr_wide.c_str());
		if (key_utf8 && val_utf8)
		{
			// the context prefix keeps lookups uniform with .mo entries
			std::string key;
			key.reserve(strlen(context) + 1 + strlen(key_utf8));
			key.append(context).append(1, '\004').append(key_utf8);
			s_winui_map.insert_or_assign(std::move(key), std::string(val_utf8));
			merged++;
		}
		if (key_utf8)
			free(key_utf8);
		if (val_utf8)
			free(val_utf8);
	}

	osd_printf_verbose("Merged %u legacy translated strings from file\n", merged);
}


//============================================================
//  load_translation - resolve the language from the global
//  options and load the .mo dictionary (GUI phase)
//============================================================

void winui_init_translation()
{
	if (s_loaded)
		return;
	s_loaded = true;

	std::string const lang = MameUIGlobal().value(OPTION_LANGUAGE);
	std::string const name = winui_plus_lang_shortname(lang);
	std::string const longname = winui_plus_lang_longname(lang);

	// MAMEPlus route takes priority: the legacy "lang" path with short
	// directory names (lang/zh_CN) is where old MAMEPlus looked, so it
	// is searched first; the official long-name directory
	// (language/Chinese_Simplified) is the low-priority fallback
	std::string const searchpath = std::string("lang;") + MameUIGlobal().value(OPTION_LANGUAGEPATH);
	emu_file file(searchpath, OPEN_FLAG_READ);

	// dedicated GUI dictionary (the official strings.mo stays untouched);
	// try the legacy short directory first, then the long one
	if (file.open(name + PATH_SEPARATOR "winui.mo") && file.open(longname + PATH_SEPARATOR "winui.mo"))
	{
		osd_printf_verbose("No GUI translation file for language %s\n", name.c_str());
	}
	else
	{
		osd_printf_verbose("Loading GUI translation file %s\n", file.fullpath());
		load_winui_mo(file);
	}

	// legacy MAMEPlus .mmo dictionaries ride on top of winui.mo; the
	// GUI reads the wide-string side of each file. Game titles go
	// under the "lst" context so they can never collide with GUI text.
	static const struct
	{
		const char *context;
		const char *base;
	} mmo_files[] =
	{
		{ "winui", "ui" },
		{ "winui", "windows" },
		{ "winui", "manufact" },
		{ "winui", "Artwork" },
		{ "winui", "Category" },
		{ "winui", "Favorites" },
		{ "winui", "IPS" },
		{ "winui", "Version" },
		{ "lst",   "lst" },
	};
	for (const auto &entry : mmo_files)
	{
		// legacy short-name directory (lang/zh_CN) first, long-name
		// directory (language/Chinese_Simplified) as the fallback
		if (file.open(name + PATH_SEPARATOR + std::string(entry.base) + ".mmo")
				&& file.open(longname + PATH_SEPARATOR + std::string(entry.base) + ".mmo"))
			continue;
		osd_printf_verbose("Loading legacy translation file %s\n", file.fullpath());
		load_mmo_file(file, entry.context);
	}
}


//============================================================
//  winui_reload_translation - re-read the dictionary after a
//  language change (callers are responsible for redrawing UI
//  text that was already rewritten)
//============================================================

void winui_reload_translation()
{
	s_loaded = false;
	s_winui_map.clear();
	s_tstring_cache.clear();
	s_utf8_cache.clear();
	winui_init_translation();
}


//============================================================
//  translate_text - look up a single string, preserving any
//  "\t<accelerator>" suffix
//============================================================

static const std::string *winui_lookup(const std::string &key)
{
	auto const found = s_winui_map.find(key);
	return (s_winui_map.end() != found) ? &found->second : nullptr;
}

static std::wstring translate_text(const std::wstring &src)
{
	std::wstring head = src;
	std::wstring tail;
	size_t const tab = src.find(L'\t');
	if (tab != std::wstring::npos)
	{
		head = src.substr(0, tab);
		tail = src.substr(tab);
	}

	char *const key_utf8 = ui_utf8_from_wstring(head.c_str());
	if (!key_utf8)
		return src;

	std::string const plain_key(key_utf8);

	// msgctxt "winui" entries are stored under "winui\004<message>"
	std::string key;
	key.reserve(6 + plain_key.size());
	key.append("winui");
	key.append(1, '\004');
	key.append(plain_key);

	std::string const *const tr = winui_lookup(key);
	std::string const tr_utf8(tr ? *tr : std::string());
	free(key_utf8);

	// untranslated or identical: keep the original
	if (tr_utf8.empty() || tr_utf8 == plain_key)
		return src;

	TCHAR *const tr_wide = ui_wstring_from_utf8(tr_utf8.c_str());
	if (!tr_wide)
		return src;
	std::wstring const result = std::wstring(tr_wide) + tail;
	free(tr_wide);
	return result;
}


//============================================================
//  winui_translate_menu - recursively rewrite menu items
//============================================================

void winui_translate_menu(HMENU hMenu)
{
	if (!hMenu)
		return;

	for (int i = GetMenuItemCount(hMenu) - 1; i >= 0; i--)
	{
		HMENU const sub = GetSubMenu(hMenu, i);
		if (sub)
			winui_translate_menu(sub);

		WCHAR buffer[512];
		MENUITEMINFOW mii;
		ZeroMemory(&mii, sizeof(mii));
		mii.cbSize     = sizeof(mii);
		mii.fMask      = MIIM_STRING | MIIM_FTYPE;
		mii.dwTypeData = buffer;
		mii.cch        = std::size(buffer) - 1;
		buffer[0]      = L'\0';

		if (!GetMenuItemInfoW(hMenu, i, TRUE, &mii))
			continue;
		if (mii.fType & MFT_SEPARATOR)
			continue;
		if (!buffer[0])
			continue;

		std::wstring const translated = translate_text(buffer);
		if (translated == buffer)
			continue;

		mii.fMask      = MIIM_STRING;
		mii.dwTypeData = const_cast<LPWSTR>(translated.c_str());
		mii.cch        = (UINT)translated.size();
		SetMenuItemInfoW(hMenu, i, TRUE, &mii);
	}
}


//============================================================
//  child window enumeration
//============================================================

static BOOL CALLBACK TranslateChildProc(HWND child, LPARAM)
{
	WCHAR buffer[512];
	if (GetWindowTextW(child, buffer, std::size(buffer) - 1) && buffer[0])
	{
		std::wstring const translated = translate_text(buffer);
		if (translated != buffer)
			SetWindowTextW(child, translated.c_str());
	}
	return TRUE;
}


//============================================================
//  winui_translate_window - caption plus all child controls
//============================================================

void winui_translate_window(HWND hwnd)
{
	if (!hwnd)
		return;

	WCHAR buffer[512];
	if (GetWindowTextW(hwnd, buffer, std::size(buffer) - 1) && buffer[0])
	{
		std::wstring const translated = translate_text(buffer);
		if (translated != buffer)
			SetWindowTextW(hwnd, translated.c_str());
	}

	EnumChildWindows(hwnd, TranslateChildProc, 0);
}


//============================================================
//  winui_translate_tstring - translate one runtime string,
//  caching results (tree items, list columns, ...)
//============================================================

const TCHAR *winui_translate_tstring(const TCHAR *src)
{
	if (!src || !src[0] || s_winui_map.empty())
		return src;

	std::wstring const key(src);
	auto const cached = s_tstring_cache.find(key);
	if (s_tstring_cache.end() != cached)
		return cached->second.c_str();

	std::wstring const translated = translate_text(key);
	if (translated != key)
		return s_tstring_cache.emplace(key, translated).first->second.c_str();

	// remember untranslated too, to keep the cache complete
	return s_tstring_cache.emplace(key, key).first->second.c_str();
}


//============================================================
//  winui_translate_utf8 - translate one UTF-8 string in a
//  given context, caching results (game titles from lst.mmo)
//============================================================

std::string winui_translate_utf8(const char *src, const char *context)
{
	if (!src || !src[0] || s_winui_map.empty())
		return std::string(src ? src : "");

	std::string key;
	key.reserve(strlen(context) + 1 + strlen(src));
	key.append(context).append(1, '\004').append(src);

	auto const cached = s_utf8_cache.find(key);
	if (s_utf8_cache.end() != cached)
		return cached->second;

	auto const found = s_winui_map.find(key);
	if (s_winui_map.end() == found)
		return s_utf8_cache.emplace(std::move(key), std::string(src)).first->second;
	return s_utf8_cache.emplace(std::move(key), found->second).first->second;
}


//============================================================
//  Options > Language menu (inserted at runtime, like MAMEPlus)
//============================================================

bool winui_insert_language_menu(HMENU hMenuBar)
{
	if (!hMenuBar)
		return false;

	// locate the Options popup: the top-level submenu holding
	// ID_OPTIONS_INTERFACE
	HMENU options = NULL;
	int const menu_count = GetMenuItemCount(hMenuBar);
	for (int i = 0; !options && (menu_count > i); i++)
	{
		HMENU const sub = GetSubMenu(hMenuBar, i);
		int const sub_count = sub ? GetMenuItemCount(sub) : 0;
		for (int j = 0; sub && (sub_count > j); j++)
		{
			MENUITEMINFOW mi;
			ZeroMemory(&mi, sizeof(mi));
			mi.cbSize = sizeof(mi);
			mi.fMask  = MIIM_ID;
			if (GetMenuItemInfoW(sub, j, TRUE, &mi) && (mi.wID == ID_OPTIONS_INTERFACE))
			{
				options = sub;
				break;
			}
		}
	}
	if (!options)
		return false;

	// remove a previously inserted popup (identified by its own ID)
	int const opt_count = GetMenuItemCount(options);
	for (int j = opt_count - 1; j >= 0; j--)
	{
		MENUITEMINFOW mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize = sizeof(mi);
		mi.fMask  = MIIM_ID | MIIM_SUBMENU;
		if (GetMenuItemInfoW(options, j, TRUE, &mi) && (mi.wID == ID_LANGUAGE_MENU))
		{
			DeleteMenu(options, j, MF_BYPOSITION);
			break;
		}
	}

	// scan for usable languages: the legacy Plus directories under
	// "lang" (short names) come first, the official long-name
	// directories under the language path are the fallback; stored
	// values are always MAMEPlus short names so old MAMEPlus builds
	// sharing the ini keep working
	s_menu_langs.clear();
	s_menu_langs.push_back("zh_CN"); // first entry: the default (Simplified Chinese)
	auto push_lang = [] (std::string &&dir)
	{
		std::string const shortname = winui_plus_lang_shortname(dir);
		// the first entry already covers Simplified Chinese
		if (!strcmp(shortname.c_str(), "zh_CN"))
			return;
		if (std::find(s_menu_langs.begin(), s_menu_langs.end(), shortname) == s_menu_langs.end())
			s_menu_langs.push_back(std::move(shortname));
	};

	WIN32_FIND_DATAA fd;
	HANDLE const find_lang = FindFirstFileA("lang\\*", &fd);
	if (find_lang != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
				continue;
			push_lang(fd.cFileName);
		}
		while (FindNextFileA(find_lang, &fd));
		FindClose(find_lang);
	}

	std::string const langpath = MameUIGlobal().value(OPTION_LANGUAGEPATH);
	HANDLE const find_official = FindFirstFileA((langpath + "\\*").c_str(), &fd);
	if (find_official != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !strcmp(fd.cFileName, ".") || !strcmp(fd.cFileName, ".."))
				continue;
			std::string dir(langpath + "\\" + fd.cFileName);
			if ((GetFileAttributesA((dir + "\\winui.mo").c_str()) == INVALID_FILE_ATTRIBUTES)
					&& (GetFileAttributesA((dir + "\\strings.mo").c_str()) == INVALID_FILE_ATTRIBUTES))
				continue;
			push_lang(fd.cFileName);
		}
		while (FindNextFileA(find_official, &fd));
		FindClose(find_official);
	}

	// current selection normalized to the short name for the radio check
	std::string const current = winui_plus_lang_shortname(MameUIGlobal().value(OPTION_LANGUAGE));

	HMENU const popup = CreatePopupMenu();
	for (size_t i = 0; s_menu_langs.size() > i; i++)
	{
		std::string const &lang = s_menu_langs[i];
		std::wstring label = ((0 == i)
				? L"Default (Simplified Chinese)"
				: std::wstring(lang.begin(), lang.end())); // ASCII short names
		MENUITEMINFOW mi;
		ZeroMemory(&mi, sizeof(mi));
		mi.cbSize     = sizeof(mi);
		mi.fMask      = MIIM_ID | MIIM_STRING | MIIM_STATE;
		mi.wID        = (UINT)(ID_LANGUAGE_FIRST + i);
		mi.dwTypeData = &label[0];
		mi.fState     = (((0 == i) ? (current == "zh_CN") : (current == lang)) ? MFS_CHECKED : MFS_ENABLED);
		InsertMenuItemW(popup, (UINT)i, TRUE, &mi);
	}

	MENUITEMINFOW mi;
	ZeroMemory(&mi, sizeof(mi));
	mi.cbSize     = sizeof(mi);
	mi.fMask      = MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
	mi.wID        = ID_LANGUAGE_MENU;
	mi.hSubMenu   = popup;
	std::wstring label = L"&Language";
	mi.dwTypeData = &label[0];
	InsertMenuItemW(options, GetMenuItemCount(options), TRUE, &mi);
	return true;
}


bool winui_handle_language_command(int id)
{
	if ((id < ID_LANGUAGE_FIRST) || (id >= ID_LANGUAGE_FIRST + (int)s_menu_langs.size()))
		return false;

	winui_apply_language(s_menu_langs[(size_t)(id - ID_LANGUAGE_FIRST)]);
	return true;
}


//============================================================
//  CBT hook - every dialog created on this thread gets
//  translated when it becomes active (property sheets,
//  directories, about, message boxes, ...)
//============================================================

static LRESULT CALLBACK CbtHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HCBT_ACTIVATE)
	{
		CBTACTIVATESTRUCT const *const info = reinterpret_cast<CBTACTIVATESTRUCT const *>(lParam);
		if (info && info->hWndActive)
			winui_translate_window(info->hWndActive);
	}
	return CallNextHookEx(s_cbt_hook, nCode, wParam, lParam);
}

void winui_install_translate_hook()
{
	if (s_cbt_hook)
		return;
	s_cbt_hook = SetWindowsHookExW(WH_CBT, CbtHookProc, NULL, GetCurrentThreadId());
}
