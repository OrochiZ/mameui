// license:BSD-3-Clause
// copyright-holders:MAME Team
//============================================================
//
//  version.cpp - version string globals
//
//============================================================

#define LONG_BUILD_VERSION "0.287.0"
#define BARE_BUILD_VERSION "0.287"
#define BARE_VCS_REVISION "2026/10/03"

extern const char bare_build_version[];
extern const char long_build_version[];
extern const char bare_vcs_revision[];
extern const char build_version[];

const char bare_build_version[] = BARE_BUILD_VERSION;
const char long_build_version[] = LONG_BUILD_VERSION;
const char bare_vcs_revision[] = BARE_VCS_REVISION;
const char build_version[] = LONG_BUILD_VERSION " (" BARE_VCS_REVISION ")";
