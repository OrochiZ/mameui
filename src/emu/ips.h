// license:BSD-3-Clause
/***************************************************************************

    ips.h

    IPS patch support.

    Ported from MAMEPlus 0.159 (src/emu/ips.c).

***************************************************************************/

#ifndef MAME_EMU_IPS_H
#define MAME_EMU_IPS_H

#pragma once

#include "emucore.h"


class running_machine;
class rom_load_manager;
class rom_entry;

// parse the -ips option and preload every patch referenced by its index files;
// call before processing the ROM region list
bool open_ips_entry(running_machine &machine, rom_load_manager &loader);

// release preloaded patch data after the ROM region list has been processed
void close_ips_entry(rom_load_manager &loader);

// bind pending patch chunks to a ROM entry by name; returns an opaque handle
// to pass to apply_ips_patch(), or nullptr if nothing applies
void *assign_ips_patch(const rom_entry *romp);

// feed patch data into a buffer freshly read from a ROM file
void apply_ips_patch(void *patch, u8 *buffer, u32 length);

#endif // MAME_EMU_IPS_H
