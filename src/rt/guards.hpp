// rt/guards - RAII guards for the OS / hardware resources of the runtime (ROADMAP 9.2b, docs/RAII.md).
//
// Rule of five (CLAUDE.md): each guard declares destructor + deleted copy and move.  FileHandle is the one that may leave a
// scope (returned by an open function), so it has a real move constructor.  Stack objects only: global/static guards would
// never be constructed (global constructors do not run here).  No exceptions: the destructors run on scope exit and on every
// `return`.  tests/test_raii.py flags new raw acquire/release pairs outside this file.
#pragma once

#include <ace/types.h>
#include <ace/managers/memory.h>
#include <ace/managers/system.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "engine/guard.hpp"
#include "rt/files.hpp"

namespace rt {

// ---- OS access: ACE gives the system back while DOS / allocations are needed (systemUse / systemUnuse) -----------------
class SystemAccess {
public:
	SystemAccess() { systemUse(); }
	~SystemAccess() { systemUnuse(); }
	SystemAccess(const SystemAccess &) = delete;
	SystemAccess &operator=(const SystemAccess &) = delete;
	SystemAccess(SystemAccess &&) = delete;
	SystemAccess &operator=(SystemAccess &&) = delete;
};

// OS access plus the blitter handed to the OS (dos.library reads on a floppy / HD may use it): the bracket every file
// read, the datadump and the IRQ trace flush use.
class OsAccess {
public:
	OsAccess() { systemUse(); systemReleaseBlitterToOs(); }
	~OsAccess() { systemGetBlitterFromOs(); systemUnuse(); }
	OsAccess(const OsAccess &) = delete;
	OsAccess &operator=(const OsAccess &) = delete;
	OsAccess(OsAccess &&) = delete;
	OsAccess &operator=(OsAccess &&) = delete;
};

// No "insert volume" requesters for the scope (absent floppies, write-protected boot disk): pr_WindowPtr = -1, restored after.
class NoRequesters {
public:
	NoRequesters() : m_pProc(reinterpret_cast<struct Process *>(FindTask(nullptr))), m_pOld(m_pProc->pr_WindowPtr) {
		m_pProc->pr_WindowPtr = reinterpret_cast<APTR>(-1);
	}
	~NoRequesters() { m_pProc->pr_WindowPtr = m_pOld; }
	NoRequesters(const NoRequesters &) = delete;
	NoRequesters &operator=(const NoRequesters &) = delete;
	NoRequesters(NoRequesters &&) = delete;
	NoRequesters &operator=(NoRequesters &&) = delete;
private:
	struct Process *m_pProc;
	APTR m_pOld;
};

// ---- a dos.library file handle (Open / Close); needs OsAccess around it -------------------------------------------------
class DosHandle {
public:
	// isQuiet: no "insert volume" requester while opening (NoRequesters for the Open call only)
	DosHandle(const char *szPath, LONG lMode, bool isQuiet = false) : m_fh(0) {
		if(isQuiet) {
			NoRequesters sQuiet;
			m_fh = Open(reinterpret_cast<CONST_STRPTR>(szPath), lMode);
		}
		else {
			m_fh = Open(reinterpret_cast<CONST_STRPTR>(szPath), lMode);
		}
	}
	~DosHandle() { close(); }
	DosHandle(const DosHandle &) = delete;
	DosHandle &operator=(const DosHandle &) = delete;
	DosHandle(DosHandle &&) = delete;
	DosHandle &operator=(DosHandle &&) = delete;
	explicit operator bool() const { return m_fh != 0; }
	BPTR get() const { return m_fh; }
	void close() { if(m_fh) { Close(m_fh); m_fh = 0; } }
	BPTR release() { const BPTR fh = m_fh; m_fh = 0; return fh; }   // the caller owns it now (adfdisks keeps the image open)
private:
	BPTR m_fh;
};

// ---- a dos.library lock (Lock / UnLock) ---------------------------------------------------------------------------------
class DosLock {
public:
	DosLock(const char *szPath, LONG lMode, bool isQuiet = false) : m_lk(0) {   // isQuiet: no requester during the Lock call
		if(isQuiet) {
			NoRequesters sQuiet;
			m_lk = Lock(reinterpret_cast<CONST_STRPTR>(szPath), lMode);
		}
		else {
			m_lk = Lock(reinterpret_cast<CONST_STRPTR>(szPath), lMode);
		}
	}
	~DosLock() { if(m_lk) UnLock(m_lk); }
	DosLock(const DosLock &) = delete;
	DosLock &operator=(const DosLock &) = delete;
	DosLock(DosLock &&) = delete;
	DosLock &operator=(DosLock &&) = delete;
	explicit operator bool() const { return m_lk != 0; }
	BPTR get() const { return m_lk; }
private:
	BPTR m_lk;
};

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

// ---- memory taken from the OS (memAlloc ... memFree) -------------------------------------------------------------------
class MemBlock {
public:
	MemBlock() : m_p(nullptr), m_ulSize(0) {}
	MemBlock(ULONG ulSize, ULONG ulFlags) : m_p(nullptr), m_ulSize(0) { acquire(ulSize, ulFlags); }
	~MemBlock() { release(); }
	MemBlock(const MemBlock &) = delete;
	MemBlock &operator=(const MemBlock &) = delete;
	MemBlock(MemBlock &&) = delete;
	MemBlock &operator=(MemBlock &&) = delete;
	bool acquire(ULONG ulSize, ULONG ulFlags) {   // frees a block held before; false when the OS has no memory
		release();
		m_p = memAlloc(ulSize, ulFlags);
		m_ulSize = m_p ? ulSize : 0;
		return m_p != nullptr;
	}
	void release() { if(m_p) { memFree(m_p, m_ulSize); m_p = nullptr; m_ulSize = 0; } }
	void *get() const { return m_p; }
	ULONG size() const { return m_ulSize; }
	explicit operator bool() const { return m_p != nullptr; }
private:
	void *m_p;
	ULONG m_ulSize;
};

// ---- interrupts off (exec Disable / Enable) ----------------------------------------------------------------------------
class IrqOff {
public:
	IrqOff() { Disable(); }
	~IrqOff() { Enable(); }
	IrqOff(const IrqOff &) = delete;
	IrqOff &operator=(const IrqOff &) = delete;
	IrqOff(IrqOff &&) = delete;
	IrqOff &operator=(IrqOff &&) = delete;
};

// ---- the file layer for the whole run: rt_file_shutdown (open file + disk images) at scope end ---------------------------
class FileSession {
public:
	FileSession() {}
	~FileSession() { rt_file_shutdown(); }
	FileSession(const FileSession &) = delete;
	FileSession &operator=(const FileSession &) = delete;
	FileSession(FileSession &&) = delete;
	FileSession &operator=(FileSession &&) = delete;
};

MS_GUARD_PINNED(SystemAccess);
MS_GUARD_PINNED(OsAccess);
MS_GUARD_PINNED(NoRequesters);
MS_GUARD_PINNED(DosHandle);
MS_GUARD_PINNED(DosLock);
MS_GUARD_PINNED(MemBlock);
MS_GUARD_PINNED(IrqOff);
MS_GUARD_PINNED(FileSession);
MS_GUARD_MOVABLE(FileHandle);

}  // namespace rt
