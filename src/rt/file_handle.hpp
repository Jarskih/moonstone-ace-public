// rt/file_handle - RAII guard of the runtime's single sequential file slot (rt_file_open ... rt_file_close), ROADMAP 9.2b / 11.1d.
//
// Split out of rt/guards.hpp: it needs nothing but the C ABI of rt/files.hpp, so it is platform-neutral (the PC build links the same
// class over its own rt_file_* implementation), while the rest of guards.hpp brackets the AmigaOS.  guards.hpp includes this header,
// so existing users keep working.  Same rules as the other guards (docs/RAII.md): stack objects only, no copy; FileHandle is the one
// that may leave a scope, so it has a real move.
#pragma once

#include <ace/types.h>
#include "engine/guard.hpp"
#include "rt/files.hpp"

namespace rt {

// ---- the runtime's single sequential file slot (rt_file_open ... rt_file_close) -----------------------------------------
class FileHandle {
public:
	struct Exact {};   // tag: open exactly this path (rt_file_open_exact), no search / art override
	explicit FileHandle(const char *szName) : m_isOpen(rt_file_open(szName) == 0) {}
	FileHandle(const char *szPath, Exact) : m_isOpen(rt_file_open_exact(szPath) == 0) {}
	~FileHandle() { close(); }
	FileHandle(const FileHandle &) = delete;
	FileHandle &operator=(const FileHandle &) = delete;
	// moved out of its scope (an open function returning it): the source is empty and closes nothing
	FileHandle(FileHandle &&o) : m_isOpen(o.m_isOpen) { o.m_isOpen = false; }
	FileHandle &operator=(FileHandle &&o) {
		if(this != &o) { close(); m_isOpen = o.m_isOpen; o.m_isOpen = false; }
		return *this;
	}
	bool isOpen() const { return m_isOpen; }
	void close() { if(m_isOpen) { rt_file_close(); m_isOpen = false; } }   // early close, e.g. before a long decode
	void read(void *pDst, ULONG ulCount) { rt_file_read(pDst, ulCount); }
	void skip(ULONG ulCount) { rt_file_skip(ulCount); }
	ULONG size() const { return rt_file_size(); }
private:
	bool m_isOpen;
};

MS_GUARD_MOVABLE(FileHandle);

}  // namespace rt
