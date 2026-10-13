// rt/sdl/gamedata - where the PC build finds the game's data files (ROADMAP 11.9; the PC twin of rt/files.cpp's search and rt/adfdisks.cpp).
//
// Nothing original ships with the PC build: the player copies the three Moonstone ADF images next to the executable or into its disks/
// drawer (ROADMAP 10.2a/b).  A bare name (bold.f, kn1.ob, program, mog ...) is looked up like the Amiga game does:
//   1. a plain file PROGDIR:data/<name>   (an extracted install, tools/hdstage.py layout)
//   2. a plain file PROGDIR:<name>        (files copied beside the executable)
//   3. a plain file <disks dir>/<name>    (an extracted disk set in the disks drawer, or the --disks directory)
//   4. a file in an ADF image of <disks dir> or PROGDIR: (any 880 KB image that mounts and holds a Moonstone marker file), read by
//      engine/ofs, the same reader as the Amiga build (ROADMAP 10.2b).
// The floppy-drive volumes DF0:..DF3: of the Amiga search do not exist on the PC.  As on the Amiga, a hit of at most 8 bytes (disk A's
// 5-byte kn1.ob stub) only counts when nothing bigger is found, and of several ADF copies the largest wins (disk B's real kn1.ob).
// The art override drawer PROGDIR:art/ and its 6-plane checks belong to the game-level file layer (rt/files.cpp, ROADMAP 11.1c), not here.
// Each lookup opens its own file handle, so several GameFiles can be open at once (the Amiga game has one at a time).
#pragma once
#include <stdint.h>

#include "engine/guard.hpp"
#include "engine/ofs.hpp"
#include "rt/platform/files.hpp"

namespace rt {
namespace sdl {

constexpr unsigned kDisks = 3;      // Moonstone A, B, C

// Scans for the ADF images and keeps them open for the length of the session (docs/RAII.md).  szDisksDir: the drawer holding the images and/or
// extracted files (nullptr = PROGDIR:disks).  At most one session at a time.
class GameDataSession {
public:
	explicit GameDataSession(const char *szDisksDir);
	~GameDataSession();
	GameDataSession(const GameDataSession &) = delete;
	GameDataSession &operator=(const GameDataSession &) = delete;
	GameDataSession(GameDataSession &&) = delete;
	GameDataSession &operator=(GameDataSession &&) = delete;
	// Bit d set = disk 'A' + d was found.
	uint8_t diskMask() const;
};
MS_GUARD_PINNED(GameDataSession);

// One open game file, from a plain file or an ADF image.  Closed by the destructor.  The default constructor makes the empty slot that
// rt_file_open re-opens (constant-initialised, no global constructor runs).
class GameFile {
public:
	constexpr GameFile() : m_hPlain(nullptr), m_iDisk(-1), m_sAdf(), m_ulSize(0), m_ulPos(0), m_isOpen(false), m_szOrigin() {}
	explicit GameFile(const char *szName);
	~GameFile();
	// Looks szName up (see the search order above) after closing what was open; false if there is no such file.
	bool open(const char *szName);
	// A plain file at exactly this logical path (rt_file_open_exact: PROGDIR:mods/...), no search.
	bool openExact(const char *szPath);
	void close();
	uint32_t pos() const { return m_ulPos; }
	GameFile(const GameFile &) = delete;
	GameFile &operator=(const GameFile &) = delete;
	GameFile(GameFile &&) = delete;
	GameFile &operator=(GameFile &&) = delete;
	explicit operator bool() const { return m_isOpen; }
	uint32_t size() const { return m_ulSize; }
	// Up to ulCount bytes at the current position (0 at the end); advances.
	uint32_t read(void *pDst, uint32_t ulCount);
	// Absolute position, false if beyond the end.
	bool seek(uint32_t ulPos);
	// Where the file came from ("data/bold.f", "ADF A:kn1.ob"), for the log.
	const char *origin() const { return m_szOrigin; }
private:
	platform::RawFile m_hPlain;
	int m_iDisk;                    // >= 0: lives in disk image m_iDisk
	ms::AdfFile m_sAdf;
	uint32_t m_ulSize;
	uint32_t m_ulPos;
	bool m_isOpen;
	char m_szOrigin[96];
};
MS_GUARD_PINNED(GameFile);

}  // namespace sdl
}  // namespace rt
