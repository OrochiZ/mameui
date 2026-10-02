// license:BSD-3-Clause
/***************************************************************************

    ips.cpp

    IPS patch support.

    Ported from MAMEPlus 0.159 (src/emu/ips.c).

    Layout under the -ipspath directory:
      <driver>/<patch>.dat   index file: "romname ipsname [CRC(XXXXXXXX)]" lines
      <driver>/<ipsname>.ips patch data, or <driver>/<subdir>/<ipsname>.ips

***************************************************************************/

#include "emu.h"
#include "emuopts.h"
#include "ips.h"

#include "corestr.h"
#include "hash.h"
#include "language.h"
#include "romentry.h"
#include "romload.h"

#include <cstring>
#include <string>


#define IPS_SIGNATURE   "PATCH"
#define IPS_TAG_EOF     "EOF"
#define INDEX_EXT       ".dat"
#define IPS_EXT         ".ips"
#define CRC_STAG        "CRC("
#define CRC_ETAG        ")"

#define BYTE3_TO_UINT(bp) \
	(((bp[0] << 16) & 0x00ff0000) | \
	 ((bp[1] << 8)  & 0x0000ff00) | \
	 ((bp[2] << 0)  & 0x000000ff))

#define BYTE2_TO_UINT(bp) \
	(((bp[0] << 8) & 0xff00) | \
	 ((bp[1] << 0) & 0x00ff))


namespace {

struct ips_chunk
{
	ips_chunk *next;
	int offset;
	int size;
	u8 *data;
};

struct ips_entry
{
	ips_entry *next;
	std::string rom_name;
	std::string ips_name;
	ips_chunk *chunk;
	ips_chunk current;
};

ips_entry *g_ips_list;


// find a ROM entry by name in the machine's ROM definitions
const romload::file *find_rom_entry(const tiny_rom_entry *roms, const char *name)
{
	for (const romload::region &region : romload::regions(roms))
	{
		if (!region.is_romdata())
			continue;

		for (const romload::file &rom : region.get_files())
		{
			if (!core_stricmp(rom.get_name(), name))
				return &rom;
		}
	}

	return nullptr;
}


bool load_ips_file(running_machine &machine, ips_chunk **p, const char *ips_dir, const char *ips_name, rom_load_manager &loader)
{
	u32 pos = 0;
	u8 buffer[8];

	osd_printf_verbose(_("IPS: loading ips \"%s/%s%s\"\n"), ips_dir, ips_name, IPS_EXT);

	std::string fname(ips_dir);
	fname.append(PATH_SEPARATOR).append(ips_name).append(IPS_EXT);

	emu_file file(machine.options().ips_path(), OPEN_FLAG_READ);
	if (file.open(fname))
	{
		loader.ips_warning(util::string_format(_("ERROR: %s/%s: open fail\n"), ips_dir, ips_name));
		return false;
	}

	int len = int(strlen(IPS_SIGNATURE));
	if (file.read(buffer, u32(len)) != u32(len) || strncmp((const char *)buffer, IPS_SIGNATURE, len) != 0)
	{
		loader.ips_warning(util::string_format(_("ERROR: %s/%s: incorrect IPS header\n"), ips_dir, ips_name));
		file.close();
		return false;
	}

	while (!file.eof())
	{
		u32 offset;
		u16 size;
		bool bRLE = false;

		if (file.read(buffer, 3) != 3)
			goto unexpected_eof;

		if (!strncmp((const char *)buffer, IPS_TAG_EOF, 3))
			break;

		offset = BYTE3_TO_UINT(buffer);

		if (file.read(buffer, 2) != 2)
			goto unexpected_eof;

		size = BYTE2_TO_UINT(buffer);
		if (size == 0)
		{
			// RLE record: two more bytes give the repeat count, a third one the fill value
			if (file.read(buffer, 3) != 3)
				goto unexpected_eof;

			size = BYTE2_TO_UINT(buffer);
			bRLE = true;
		}

		*p = new ips_chunk;
		(*p)->data = new u8[size];

		if (bRLE)
			memset((*p)->data, buffer[2], size);
		else if (file.read((*p)->data, size) != size)
			goto unexpected_eof;

		// store offsets relative to the previous chunk so patches can be
		// streamed across the chunked reads performed by read_rom_data()
		offset -= pos;
		(*p)->offset = int(offset);
		(*p)->size = int(size);
		(*p)->next = nullptr;

		p = &(*p)->next;
		pos += offset + size;
	}

	file.close();
	return true;

unexpected_eof:
	loader.ips_warning(util::string_format(_("ERROR: %s/%s: unexpected EOF\n"), ips_dir, ips_name));
	file.close();
	return false;
}


bool check_crc(const char *crc, const std::string &rom_hash)
{
	if (!crc)
		return false;

	if (strlen(crc) != 8 + strlen(CRC_STAG) + strlen(CRC_ETAG))
		return false;

	if (strncmp(crc, CRC_STAG, strlen(CRC_STAG)) != 0)
		return false;

	if (strcmp(crc + 8 + strlen(CRC_STAG), CRC_ETAG) != 0)
		return false;

	char tmp[10];
	strncpy(tmp, crc + strlen(CRC_STAG), 8);
	tmp[8] = '\0';

	util::hash_collection ips_hash;
	ips_hash.add_from_string(util::hash_collection::HASH_CRC, std::string_view(tmp, 8));

	return ips_hash == util::hash_collection(rom_hash);
}


bool parse_ips_patch(running_machine &machine, ips_entry **ips_p, const char *patch_name, rom_load_manager &loader)
{
	char buffer[1024];

	osd_printf_verbose(_("IPS: parsing ips \"%s/%s%s\"\n"), machine.system().name, patch_name, INDEX_EXT);

	std::string fname(machine.system().name);
	fname.append(PATH_SEPARATOR).append(patch_name).append(INDEX_EXT);

	emu_file fpDat(machine.options().ips_path(), OPEN_FLAG_READ);
	if (fpDat.open(fname))
	{
		loader.ips_warning(util::string_format(_("ERROR: %s: IPS file is not found\n"), patch_name));
		return false;
	}

	bool result = false;

	while (!fpDat.eof())
	{
		if (fpDat.gets(buffer, sizeof(buffer)) != nullptr)
		{
			char *rom_name;
			const char *ips_dir;
			char *ips_name;
			char *crc;

			if (buffer[0] == '[')   // section header: per-machine entries end here
				break;

			rom_name = strtok(buffer, " \t\r\n");
			if (!rom_name)
				continue;
			if (rom_name[0] == '#')
				continue;

			const romload::file *current = find_rom_entry(machine.system().rom, rom_name);
			if (!current)
			{
				loader.ips_warning(util::string_format(_("ERROR: ROM entry \"%s\" is not found for IPS file \"%s\"\n"), rom_name, patch_name));
				goto parse_fail;
			}

			ips_name = strtok(nullptr, " \t\r\n");
			if (!ips_name)
			{
				loader.ips_warning(util::string_format(_("ERROR: IPS file is not defined for ROM entry \"%s\"\n"), rom_name));
				goto parse_fail;
			}

			crc = strtok(nullptr, "\r\n");
			strtok(nullptr, "\r\n");

			if (crc && !check_crc(crc, current->get_hashdata()))
			{
				loader.ips_warning(util::string_format(_("ERROR: wrong CRC for ROM entry \"%s\"\n"), rom_name));
				goto parse_fail;
			}

			result = true;

			if (strchr(ips_name, '\\'))
			{
				ips_dir = strtok(ips_name, "\\");
				ips_name = strtok(nullptr, "\\");
			}
			else
			{
				ips_dir = machine.system().name;
			}

			ips_entry *entry = new ips_entry;
			memset(entry, 0, sizeof(*entry));
			*ips_p = entry;
			ips_p = &entry->next;

			entry->rom_name = rom_name;
			entry->ips_name = ips_name;

			if (!load_ips_file(machine, &entry->chunk, ips_dir, entry->ips_name.c_str(), loader))
				goto parse_fail;

			if (entry->chunk == nullptr)
			{
				loader.ips_warning(util::string_format(_("ERROR: %s/%s: IPS data is empty\n"), ips_dir, entry->ips_name));
				goto parse_fail;
			}
		}
	}

	fpDat.close();
	return result;

parse_fail:
	fpDat.close();
	return false;
}


void apply_ips_patch_single(ips_chunk *p, u8 *buffer, int length)
{
	if (!p->data)
		return;

	while (true)
	{
		if (p->offset >= length)
		{
			p->offset -= length;
			return;
		}

		length -= p->offset;
		buffer += p->offset;
		p->offset = 0;

		if (p->size > length)
		{
			memcpy(buffer, p->data, length);
			p->size -= length;
			p->data += length;
			return;
		}

		memcpy(buffer, p->data, p->size);
		length -= p->size;
		buffer += p->size;

		p->size = 0;
		p->data = nullptr;

		if (!p->next)
		{
			osd_printf_verbose("IPS: apply IPS done\n");
			return;
		}

		*p = *p->next;
	}
}

} // anonymous namespace


bool open_ips_entry(running_machine &machine, rom_load_manager &loader)
{
	const char *patchname = machine.options().ips();
	bool result = true;

	g_ips_list = nullptr;

	std::string s(patchname);
	ips_entry **list = &g_ips_list;

	size_t pos = 0;
	while (pos < s.size())
	{
		size_t comma = s.find(',', pos);
		if (comma == std::string::npos)
			comma = s.size();

		std::string patch(s, pos, comma - pos);
		pos = comma + 1;
		if (patch.empty())
			continue;

		result = parse_ips_patch(machine, list, patch.c_str(), loader);
		if (!result)
			return result;

		while (*list)
			list = &(*list)->next;
	}

	return result;
}


void close_ips_entry(rom_load_manager &loader)
{
	ips_entry *p = g_ips_list;

	while (p)
	{
		ips_entry *next = p->next;

		if (p->current.data)
			loader.ips_warning(util::string_format(_("ERROR: %s: IPS is not applied correctly to ROM entry \"%s\"\n"), p->ips_name, p->rom_name));

		ips_chunk *chunk = p->chunk;
		while (chunk)
		{
			ips_chunk *next_chunk = chunk->next;
			delete[] chunk->data;
			delete chunk;
			chunk = next_chunk;
		}

		delete p;
		p = next;
	}

	g_ips_list = nullptr;
}


void *assign_ips_patch(const rom_entry *romp)
{
	const char *name = ROM_GETNAME(romp);
	bool found = false;

	for (ips_entry *p = g_ips_list; p; p = p->next)
	{
		memset(&p->current, 0, sizeof(p->current));

		if (!core_stricmp(p->rom_name.c_str(), name))
		{
			osd_printf_verbose("IPS: assign IPS file \"%s\" to ROM entry \"%s\"\n", p->ips_name.c_str(), name);
			p->current = *p->chunk;

			found = true;
		}
	}

	return found ? g_ips_list : (void *)nullptr;
}


void apply_ips_patch(void *patch, u8 *buffer, u32 length)
{
	for (ips_entry *p = (ips_entry *)patch; p; p = p->next)
		if (p->current.data)
			apply_ips_patch_single(&p->current, buffer, int(length));
}
