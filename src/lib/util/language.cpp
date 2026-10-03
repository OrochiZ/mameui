// license:BSD-3-Clause
// copyright-holders:Vas Crabb
/***************************************************************************

    language.cpp

    Multi-language support.

***************************************************************************/

#include "language.h"

#include "ioprocs.h"

#include "osdcore.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <unordered_map>
#include <utility>
#include <vector>


namespace util {

namespace {

constexpr std::uint32_t MO_MAGIC = 0x950412de;
constexpr std::uint32_t MO_MAGIC_REVERSED = 0xde120495;

// buffers holding the raw string data each loaded dictionary points into
std::vector<std::unique_ptr<std::uint32_t []> > f_translation_data;
std::unordered_map<std::string, std::pair<char const *, std::uint32_t> > f_translation_map;

bool parse_mo(random_read &file,
		std::unique_ptr<std::uint32_t []> &translation_data,
		std::unordered_map<std::string, std::pair<char const *, std::uint32_t> > &translation_map)
{
	file.seek(0, SEEK_SET);
	std::uint64_t size = 0;
	if (file.length(size) || (20 > size))
	{
		osd_printf_error("Error reading translation file: %u-byte file is too small to contain translation data\n", size);
		return false;
	}

	translation_data.reset(new (std::nothrow) std::uint32_t [(size + 3) / 4]);
	if (!translation_data)
	{
		osd_printf_error("Failed to allocate %u bytes to load translation data file\n", size);
		return false;
	}

	auto const [err, actual] = read(file, translation_data.get(), size);
	if (err || (actual != size))
	{
		osd_printf_error("Error reading translation file: requested %u bytes but got %u bytes\n", size, actual);
		translation_data.reset();
		return false;
	}

	if ((translation_data[0] != MO_MAGIC) && (translation_data[0] != MO_MAGIC_REVERSED))
	{
		osd_printf_error("Error reading translation file: unrecognized magic number 0x%08X\n", translation_data[0]);
		translation_data.reset();
		return false;
	}

	auto const fetch_word =
			[reversed = translation_data[0] == MO_MAGIC_REVERSED, words = translation_data.get()] (size_t offset)
			{
				return reversed ? swapendian_int32(words[offset]) : words[offset];
			};

	// FIXME: check major/minor version number

	if ((fetch_word(3) % 4) || (fetch_word(4) % 4))
	{
		osd_printf_error("Error reading translation file: table offsets %u and %u are not word-aligned\n", fetch_word(3), fetch_word(4));
		translation_data.reset();
		return false;
	}

	std::uint32_t const number_of_strings = fetch_word(2);
	std::uint32_t const original_table_offset = fetch_word(3) >> 2;
	std::uint32_t const translation_table_offset = fetch_word(4) >> 2;
	if ((4 * (original_table_offset + (std::uint64_t(number_of_strings) * 2))) > size)
	{
		osd_printf_error("Error reading translation file: %u-entry original string table at offset %u extends past end of %u-byte file\n", number_of_strings, fetch_word(3), size);
		translation_data.reset();
		return false;
	}
	if ((4 * (translation_table_offset + (std::uint64_t(number_of_strings) * 2))) > size)
	{
		osd_printf_error("Error reading translation file: %u-entry translated string table at offset %u extends past end of %u-byte file\n", number_of_strings, fetch_word(4), size);
		translation_data.reset();
		return false;
	}
	osd_printf_verbose("Reading translation file: %u strings, original table at word offset %u, translated table at word offset %u\n", number_of_strings, original_table_offset, translation_table_offset);

	char const *const data = reinterpret_cast<char const *>(translation_data.get());
	for (std::uint32_t i = 1; number_of_strings > i; ++i)
	{
		std::uint32_t const original_length = fetch_word(original_table_offset + (2 * i));
		std::uint32_t const original_offset = fetch_word(original_table_offset + (2 * i) + 1);
		if ((original_length + original_offset) >= size)
		{
			osd_printf_error("Error reading translation file: %u-byte original string %u at offset %u extends past end of %u-byte file\n", original_length, i, original_offset, size);
			continue;
		}
		if (data[original_length + original_offset])
		{
			osd_printf_error("Error reading translation file: %u-byte original string %u at offset %u is not correctly NUL-terminated\n", original_length, i, original_offset);
			continue;
		}

		std::uint32_t const translation_length = fetch_word(translation_table_offset + (2 * i));
		std::uint32_t const translation_offset = fetch_word(translation_table_offset + (2 * i) + 1);
		if ((translation_length + translation_offset) >= size)
		{
			osd_printf_error("Error reading translation file: %u-byte translated string %u at offset %u extends past end of %u-byte file\n", translation_length, i, translation_offset, size);
			continue;
		}
		if (data[translation_length + translation_offset])
		{
			osd_printf_error("Error reading translation file: %u-byte translated string %u at offset %u is not correctly NUL-terminated\n", translation_length, i, translation_offset);
			continue;
		}

		std::string_view const original(&data[original_offset], original_length);
		char const *const translation(&data[translation_offset]);
		auto const ins = translation_map.emplace(std::string(original), std::make_pair(translation, translation_length));
		if (!ins.second)
		{
			osd_printf_warning(
					"Loading translation file: translation %u '%s'='%s' conflicts with previous translation '%s'='%s'\n",
					i,
					original,
					translation,
					ins.first->first,
					ins.first->second.first);
		}
	}

	osd_printf_verbose("Loaded %u translated string from file\n", translation_map.size());
	return true;
}


// MAMEPlus legacy .mmo dictionary: a header of three INT32s (zero
// placeholder, format version 3 and the message count), followed by four
// string-area offsets per message (original/translated in UTF-8, then
// wide original/translated in UTF-16LE) and a byte count for the string
// area itself. Only the UTF-8 pair is used for the internal UI.
bool parse_mmo(random_read &file,
		std::unique_ptr<std::uint32_t []> &translation_data,
		std::unordered_map<std::string, std::pair<char const *, std::uint32_t> > &translation_map)
{
	file.seek(0, SEEK_SET);
	std::uint64_t size = 0;
	if (file.length(size) || (16 > size))
	{
		osd_printf_error("Error reading translation file: %u-byte file is too small to contain translation data\n", size);
		return false;
	}

	translation_data.reset(new (std::nothrow) std::uint32_t [(size + 3) / 4]);
	if (!translation_data)
	{
		osd_printf_error("Failed to allocate %u bytes to load translation data file\n", size);
		return false;
	}

	auto const [err, actual] = read(file, translation_data.get(), size);
	if (err || (actual != size))
	{
		osd_printf_error("Error reading translation file: requested %u bytes but got %u bytes\n", size, actual);
		translation_data.reset();
		return false;
	}

	std::uint32_t const number_of_strings = translation_data[2];
	if ((translation_data[0] != 0) || (translation_data[1] != 3) || !number_of_strings)
	{
		osd_printf_error("Error reading translation file: unrecognized header (placeholder %u, version %u)\n", translation_data[0], translation_data[1]);
		translation_data.reset();
		return false;
	}

	std::uint64_t const string_base = 12 + (std::uint64_t(number_of_strings) * 16) + 4;
	if (string_base > size)
	{
		osd_printf_error("Error reading translation file: %u-entry index extends past end of %u-byte file\n", number_of_strings, size);
		translation_data.reset();
		return false;
	}

	std::uint32_t const str_size = translation_data[(string_base - 4) / 4];
	if ((string_base + str_size) > size)
	{
		osd_printf_error("Error reading translation file: %u-byte string area extends past end of %u-byte file\n", str_size, size);
		translation_data.reset();
		return false;
	}

	char const *const strings = reinterpret_cast<char const *>(translation_data.get()) + string_base;
	for (std::uint32_t i = 0; number_of_strings > i; ++i)
	{
		std::uint32_t const uid_offset = translation_data[3 + (4 * i)];
		std::uint32_t const ustr_offset = translation_data[3 + (4 * i) + 1];
		if ((uid_offset >= str_size) || (ustr_offset >= str_size))
			continue;

		char const *const uid = strings + uid_offset;
		char const *const ustr = strings + ustr_offset;
		char const *const uid_end = reinterpret_cast<char const *>(memchr(uid, '\0', str_size - uid_offset));
		char const *const ustr_end = reinterpret_cast<char const *>(memchr(ustr, '\0', str_size - ustr_offset));
		if (!uid_end || !ustr_end || !uid[0] || !ustr[0])
			continue;

		translation_map.emplace(std::string(uid, uid_end - uid), std::make_pair(ustr, ustr_end - ustr));
	}

	osd_printf_verbose("Loaded %u translated string from file\n", translation_map.size());
	return true;
}

} // anonymous namespace


void unload_translation()
{
	f_translation_data.clear();
	f_translation_map.clear();
}


void load_translation(random_read &file)
{
	std::unique_ptr<std::uint32_t []> translation_data;
	std::unordered_map<std::string, std::pair<char const *, std::uint32_t> > translation_map;

	if (!parse_mo(file, translation_data, translation_map))
		return;

	f_translation_data.clear();
	f_translation_data.push_back(std::move(translation_data));
	f_translation_map = std::move(translation_map);
}


void merge_translation(random_read &file, char const *context)
{
	std::unique_ptr<std::uint32_t []> translation_data;
	std::unordered_map<std::string, std::pair<char const *, std::uint32_t> > translation_map;

	// gettext .mo files start with the magic number, legacy .mmo files
	// with a zero placeholder word
	std::uint32_t magic = 0;
	if (read(file, &magic, sizeof(magic)).first || ((MO_MAGIC != magic) && (MO_MAGIC_REVERSED != magic)))
	{
		file.seek(0, SEEK_SET);
		if (!parse_mmo(file, translation_data, translation_map))
			return;
	}
	else if (!parse_mo(file, translation_data, translation_map))
	{
		return;
	}

	// merge into the existing dictionary; later entries override earlier ones
	for (auto &entry : translation_map)
	{
		if (context)
		{
			std::string key;
			key.reserve(std::strlen(context) + 1 + entry.first.size());
			key.append(context);
			key.append(1, '\004');
			key.append(entry.first);
			f_translation_map.insert_or_assign(std::move(key), entry.second);
		}
		else
		{
			f_translation_map.insert_or_assign(std::move(entry.first), entry.second);
		}
	}
	f_translation_data.push_back(std::move(translation_data));
}


char const *lang_translate(char const *message)
{
	auto const found = f_translation_map.find(message);
	if (f_translation_map.end() != found)
		return found->second.first;
	return message;
}


std::string_view lang_translate(std::string_view message)
{
	auto const found = f_translation_map.find(std::string(message));
	if (f_translation_map.end() != found)
		return std::string_view(found->second.first, found->second.second);
	return message;
}


char const *lang_translate(char const *context, char const *message)
{
	if (!f_translation_map.empty())
	{
		auto const ctxlen(std::strlen(context));
		auto const msglen(std::strlen(message));
		std::string key;
		key.reserve(ctxlen + 1 + msglen);
		key.append(context, ctxlen);
		key.append(1, '\004');
		key.append(message, msglen);
		auto const found = f_translation_map.find(key);
		if (f_translation_map.end() != found)
			return found->second.first;
	}
	return message;
}


std::string_view lang_translate(char const *context, std::string_view message)
{
	return lang_translate(std::string_view(context), message);
}


std::string_view lang_translate(std::string_view context, std::string_view message)
{
	if (!f_translation_map.empty())
	{
		std::string key;
		key.reserve(context.length() + 1 + message.length());
		key.append(context);
		key.append(1, '\004');
		key.append(message);
		auto const found = f_translation_map.find(key);
		if (f_translation_map.end() != found)
			return std::string_view(found->second.first, found->second.second);
	}
	return message;
}

} // namespace util
