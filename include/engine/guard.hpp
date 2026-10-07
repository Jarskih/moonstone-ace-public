// engine/guard - pure RAII helpers (ROADMAP 9.2b, docs/RAII.md, CLAUDE.md "Resources use RAII").
//
// Rule of five (owner 2026-10-07): a guard that owns a resource declares all five special members explicitly: the destructor
// releases; copy constructor, copy assignment, move constructor and move assignment are deleted.  A guard that must leave its
// scope gets a real move constructor that transfers ownership and empties the source (see rt/guards.hpp FileHandle).  Plain
// data structs stay rule of zero (nothing declared).
//
// Guards live on the stack only: global and static constructors do not run in this program (docs/MEMORY.md), and there are
// no exceptions, so destructors run on scope exit and on every `return`.  Return a guard with `return Guard(...)`
// (C++17 guaranteed copy elision, no move needed).  No STL: the type traits below use compiler builtins.
#pragma once
#include <stdint.h>
#include "engine/memstack.hpp"

namespace ms {

// ---- compile-time traits without <type_traits> ------------------------------------------------------------------------
template <class T> struct GuardTraits {
	static constexpr bool kCopyConstructible = __is_constructible(T, const T &);
	static constexpr bool kMoveConstructible = __is_constructible(T, T &&);
	static constexpr bool kCopyAssignable = __is_assignable(T &, const T &);
	static constexpr bool kMoveAssignable = __is_assignable(T &, T &&);
};
// Every guard that cannot be moved out of its scope.
#define MS_GUARD_PINNED(T) \
	static_assert(!::ms::GuardTraits<T>::kCopyConstructible && !::ms::GuardTraits<T>::kCopyAssignable \
		&& !::ms::GuardTraits<T>::kMoveConstructible && !::ms::GuardTraits<T>::kMoveAssignable, #T " must be a pinned guard (rule of five, all deleted)")
// A guard that may be moved (ownership transfer) but never copied.
#define MS_GUARD_MOVABLE(T) \
	static_assert(!::ms::GuardTraits<T>::kCopyConstructible && !::ms::GuardTraits<T>::kCopyAssignable \
		&& ::ms::GuardTraits<T>::kMoveConstructible, #T " must be move-only (rule of five)")

// ---- ScopeExit: run a callable when the scope ends ---------------------------------------------------------------------
// Usage:  auto sUndo = scopeExit([&] { undo(); });   ...   sUndo.dismiss();  // when the work succeeded and must stay
template <class F> class ScopeExit {
public:
	explicit ScopeExit(F fn) : m_fn(fn), m_isActive(true) {}
	~ScopeExit() { if(m_isActive) m_fn(); }
	ScopeExit(const ScopeExit &) = delete;
	ScopeExit &operator=(const ScopeExit &) = delete;
	ScopeExit(ScopeExit &&) = delete;
	ScopeExit &operator=(ScopeExit &&) = delete;
	void dismiss() { m_isActive = false; }
private:
	F m_fn;
	bool m_isActive;
};
template <class F> ScopeExit<F> scopeExit(F fn) { return ScopeExit<F>(fn); }   // guaranteed elision, no move

// ---- MemMark: take a MemStack mark, release back to it at scope end ------------------------------------------------------
// For scratch memory that lives inside one function.  keep() leaves the allocations in place (the scene manager's per-level
// marks are the long-lived kind: they cross enter/leave calls, docs/RAII.md "exempt").
class MemMark {
public:
	explicit MemMark(MemStack &s) : m_s(s), m_ulMark(memStackMark(s)), m_isActive(true) {}
	~MemMark() { if(m_isActive) memStackRelease(m_s, m_ulMark); }
	MemMark(const MemMark &) = delete;
	MemMark &operator=(const MemMark &) = delete;
	MemMark(MemMark &&) = delete;
	MemMark &operator=(MemMark &&) = delete;
	uint32_t mark() const { return m_ulMark; }
	void keep() { m_isActive = false; }
private:
	MemStack &m_s;
	uint32_t m_ulMark;
	bool m_isActive;
};

MS_GUARD_PINNED(MemMark);

}  // namespace ms
