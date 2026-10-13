// rt/bsdsocket_calls - the bsdsocket.library v4 calls the net transport uses (ROADMAP 12.2).
//
// Why not <proto/bsdsocket.h>: the toolchain's copy is the old fd2pragma form (`jsr a6@(-30:W)`) which this assembler rejects,
// and its own LPn macros are not generated for bsdsocket.  So the stubs are written here, one inline asm each.
//
// SOURCE of the offsets and register assignments: Olaf Barthel's Roadshow SDK, bsdsocket_lib.sfd, as shipped in the toolchain
// as D:/Amiga/tools/amiga-debug/extension/bin/win32/opt/m68k-amiga-elf/sys-include/inline/bsdsocket.h (fd2pragma output of
// that file; each function below was copied from there, offset and register by register):
//   socket(domain d0, type d1, protocol d2)               -30        bind(sock d0, name a0, namelen d1)            -36
//   listen(sock d0, backlog d1)                           -42        accept(sock d0, addr a0, addrlen a1)          -48
//   sendto(sock d0, buf a0, len d1, flags d2, to a1, tolen d3) -60   recvfrom(sock d0, buf a0, len d1, flags d2, from a1, fromlen a2) -72   (UDP, ROADMAP 12.21)
//   connect(sock d0, name a0, namelen d1)                 -54        send(sock d0, buf a0, len d1, flags d2)       -66
//   recv(sock d0, buf a0, len d1, flags d2)               -78        setsockopt(sock d0, level d1, opt d2, val a0, len d3) -90
//   getsockopt(sock d0, level d1, opt d2, val a0, len a1) -96        IoctlSocket(sock d0, req d1, argp a0)       -114
//   CloseSocket(sock d0)                                  -120       WaitSelect(nfds d0, rd a0, wr a1, ex a2, tv a3, sigs d1) -126
//   Errno()                                               -162       inet_addr(cp a0)                            -180
//   gethostbyname(name a0)                                -210
// d0/d1/a0/a1 are scratch across a library call (operands "+r"); d2/d3/a2/a3 are preserved by the callee (plain inputs).
#pragma once
#include <ace/types.h>
#include <exec/types.h>

struct Library;
extern struct Library *SocketBase;   // per task: opened by whoever calls (rt/net.cpp SocketLib)

namespace rt {
namespace bsd {

#define MS_BSD_JSR(off) "jsr %%a6@(-" #off ":W)"

inline LONG socket(LONG lDomain, LONG lType, LONG lProto) {
	register LONG d0 __asm("d0") = lDomain;
	register LONG d1 __asm("d1") = lType;
	register LONG d2 __asm("d2") = lProto;
	register void *a0 __asm("a0");
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(30) : "+r"(d0), "+r"(d1), "=r"(a0), "=r"(a1) : "r"(d2), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG bind(LONG lSock, const void *pName, LONG lLen) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLen;
	register const void *a0 __asm("a0") = pName;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(36) : "+r"(d0), "+r"(d1), "+r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG listen(LONG lSock, LONG lBacklog) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lBacklog;
	register void *a0 __asm("a0");
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(42) : "+r"(d0), "+r"(d1), "=r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG accept(LONG lSock, void *pAddr, ULONG *pulAddrLen) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1");
	register void *a0 __asm("a0") = pAddr;
	register ULONG *a1 __asm("a1") = pulAddrLen;
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(48) : "+r"(d0), "=r"(d1), "+r"(a0), "+r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG connect(LONG lSock, const void *pName, LONG lLen) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLen;
	register const void *a0 __asm("a0") = pName;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(54) : "+r"(d0), "+r"(d1), "+r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

// UDP (ROADMAP 12.21).  d2/d3/a2 are preserved by the callee, d0/d1/a0/a1 are not.
inline LONG sendto(LONG lSock, const void *pBuf, LONG lLen, LONG lFlags, const void *pTo, LONG lToLen) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLen;
	register LONG d2 __asm("d2") = lFlags;
	register LONG d3 __asm("d3") = lToLen;
	register const void *a0 __asm("a0") = pBuf;
	register const void *a1 __asm("a1") = pTo;
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(60) : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : "r"(d2), "r"(d3), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG recvfrom(LONG lSock, void *pBuf, LONG lLen, LONG lFlags, void *pFrom, ULONG *pulFromLen) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLen;
	register LONG d2 __asm("d2") = lFlags;
	register void *a0 __asm("a0") = pBuf;
	register void *a1 __asm("a1") = pFrom;
	register ULONG *a2 __asm("a2") = pulFromLen;
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(72) : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : "r"(d2), "r"(a2), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG send(LONG lSock, const void *pBuf, LONG lLen, LONG lFlags) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLen;
	register LONG d2 __asm("d2") = lFlags;
	register const void *a0 __asm("a0") = pBuf;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(66) : "+r"(d0), "+r"(d1), "+r"(a0), "=r"(a1) : "r"(d2), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG recv(LONG lSock, void *pBuf, LONG lLen, LONG lFlags) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLen;
	register LONG d2 __asm("d2") = lFlags;
	register void *a0 __asm("a0") = pBuf;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(78) : "+r"(d0), "+r"(d1), "+r"(a0), "=r"(a1) : "r"(d2), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG setsockopt(LONG lSock, LONG lLevel, LONG lOpt, const void *pVal, LONG lLen) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLevel;
	register LONG d2 __asm("d2") = lOpt;
	register LONG d3 __asm("d3") = lLen;
	register const void *a0 __asm("a0") = pVal;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(90) : "+r"(d0), "+r"(d1), "+r"(a0), "=r"(a1) : "r"(d2), "r"(d3), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG getsockopt(LONG lSock, LONG lLevel, LONG lOpt, void *pVal, ULONG *pulLen) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1") = lLevel;
	register LONG d2 __asm("d2") = lOpt;
	register void *a0 __asm("a0") = pVal;
	register ULONG *a1 __asm("a1") = pulLen;
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(96) : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : "r"(d2), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG IoctlSocket(LONG lSock, ULONG ulReq, void *pArg) {
	register LONG d0 __asm("d0") = lSock;
	register ULONG d1 __asm("d1") = ulReq;
	register void *a0 __asm("a0") = pArg;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(114) : "+r"(d0), "+r"(d1), "+r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG CloseSocket(LONG lSock) {
	register LONG d0 __asm("d0") = lSock;
	register LONG d1 __asm("d1");
	register void *a0 __asm("a0");
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(120) : "+r"(d0), "=r"(d1), "=r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

// pRd / pWr / pEx: fd_set pointers or null; pTv: struct __timeval {ULONG secs, micro} or null (wait for ever); pulSigs: or null.
inline LONG WaitSelect(LONG lNfds, void *pRd, void *pWr, void *pEx, void *pTv, ULONG *pulSigs) {
	register LONG d0 __asm("d0") = lNfds;
	register ULONG *d1 __asm("d1") = pulSigs;
	register void *a0 __asm("a0") = pRd;
	register void *a1 __asm("a1") = pWr;
	register void *a2 __asm("a2") = pEx;
	register void *a3 __asm("a3") = pTv;
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(126) : "+r"(d0), "+r"(d1), "+r"(a0), "+r"(a1) : "r"(a2), "r"(a3), "r"(a6) : "cc", "memory");
	return d0;
}

inline LONG Errno() {
	register LONG d0 __asm("d0");
	register LONG d1 __asm("d1");
	register void *a0 __asm("a0");
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(162) : "=r"(d0), "=r"(d1), "=r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

inline ULONG inet_addr(const char *szCp) {
	register ULONG d0 __asm("d0");
	register LONG d1 __asm("d1");
	register const char *a0 __asm("a0") = szCp;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(180) : "=r"(d0), "=r"(d1), "+r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

// Returns a struct hostent * (see netdb.h): h_length at +10, h_addr_list at +12 (pointer to pointers); null on failure.
inline void *gethostbyname(const char *szName) {
	register void *d0 __asm("d0");
	register LONG d1 __asm("d1");
	register const char *a0 __asm("a0") = szName;
	register void *a1 __asm("a1");
	register struct Library *a6 __asm("a6") = SocketBase;
	__asm volatile(MS_BSD_JSR(210) : "=r"(d0), "=r"(d1), "+r"(a0), "=r"(a1) : "r"(a6) : "cc", "memory");
	return d0;
}

#undef MS_BSD_JSR

}  // namespace bsd
}  // namespace rt
