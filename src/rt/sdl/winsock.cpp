// rt/sdl/winsock - see winsock.hpp.  The only file of the PC build that includes the Winsock headers.
#include "rt/sdl/winsock.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)   // mstcpip.h
#endif

namespace rt {
namespace wsock {

namespace {

bool s_isUp = false;

SOCKET toSocket(int32_t lFd) { return static_cast<SOCKET>(static_cast<uint32_t>(lFd)); }
int32_t fromSocket(SOCKET sock) { return sock == INVALID_SOCKET ? -1 : static_cast<int32_t>(sock); }

sockaddr_in makeAddr(uint32_t ulIp, uint16_t uwPort) {
	sockaddr_in sAddr;
	memset(&sAddr, 0, sizeof(sAddr));
	sAddr.sin_family = AF_INET;
	sAddr.sin_port = htons(uwPort);
	sAddr.sin_addr.s_addr = htonl(ulIp);
	return sAddr;
}

bool isWouldBlock(int iErr) { return iErr == WSAEWOULDBLOCK || iErr == WSAEINPROGRESS; }

// The address every listening / datagram socket binds to.  MS_NET_BIND=loopback (or 127.0.0.1) keeps them on this machine: test runs
// with several instances on localhost then never open a port to the network, so Windows Firewall does not ask about every new exe copy.
uint32_t bindAddress() {
	const char *szBind = getenv("MS_NET_BIND");
	if(szBind != nullptr && (strcmp(szBind, "loopback") == 0 || strcmp(szBind, "127.0.0.1") == 0)) {
		return INADDR_LOOPBACK;
	}
	return INADDR_ANY;
}

}  // namespace

bool startup() {
	if(s_isUp) {
		return true;
	}
	WSADATA sData;
	s_isUp = WSAStartup(MAKEWORD(2, 2), &sData) == 0;
	return s_isUp;
}

void cleanup() {
	if(s_isUp) {
		WSACleanup();
		s_isUp = false;
	}
}

int32_t openStream() { return fromSocket(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)); }
int32_t openDgram() { return fromSocket(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)); }
void closeFd(int32_t lFd) { closesocket(toSocket(lFd)); }

void setNonBlocking(int32_t lFd, bool isStream) {
	u_long ulOn = 1;
	ioctlsocket(toSocket(lFd), FIONBIO, &ulOn);
	if(isStream) {
		BOOL iNoDelay = TRUE;
		setsockopt(toSocket(lFd), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&iNoDelay), sizeof(iNoDelay));
	}
	else {
		// an ICMP "port unreachable" for an earlier datagram would otherwise make the next recvfrom fail with WSAECONNRESET
		DWORD ulBytes = 0;
		BOOL iNoReset = FALSE;
		WSAIoctl(toSocket(lFd), SIO_UDP_CONNRESET, &iNoReset, sizeof(iNoReset), nullptr, 0, &ulBytes, nullptr, nullptr);
	}
}

bool bindAny(int32_t lFd, uint16_t uwPort, bool isExclusive) {
	if(isExclusive) {
		BOOL iOn = TRUE;
		setsockopt(toSocket(lFd), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&iOn), sizeof(iOn));
	}
	const sockaddr_in sAddr = makeAddr(bindAddress(), uwPort);
	return bind(toSocket(lFd), reinterpret_cast<const sockaddr *>(&sAddr), sizeof(sAddr)) == 0;
}

bool listenOn(int32_t lFd, int32_t lBacklog) { return listen(toSocket(lFd), lBacklog) == 0; }

int32_t acceptFrom(int32_t lListen) { return fromSocket(accept(toSocket(lListen), nullptr, nullptr)); }

int32_t connectTo(int32_t lFd, uint32_t ulIp, uint16_t uwPort) {
	const sockaddr_in sAddr = makeAddr(ulIp, uwPort);
	if(connect(toSocket(lFd), reinterpret_cast<const sockaddr *>(&sAddr), sizeof(sAddr)) == 0) {
		return 1;
	}
	return isWouldBlock(WSAGetLastError()) ? 0 : -1;
}

int32_t socketError(int32_t lFd) {
	int iErr = 0;
	int iLen = sizeof(iErr);
	getsockopt(toSocket(lFd), SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&iErr), &iLen);
	return iErr;
}

int32_t sendBytes(int32_t lFd, const void *pData, uint32_t ulLen) {
	const int iRc = send(toSocket(lFd), static_cast<const char *>(pData), static_cast<int>(ulLen), 0);
	if(iRc >= 0) {
		return iRc;
	}
	return isWouldBlock(WSAGetLastError()) ? 0 : -1;
}

int32_t recvBytes(int32_t lFd, void *pBuf, uint32_t ulCap) {
	const int iRc = recv(toSocket(lFd), static_cast<char *>(pBuf), static_cast<int>(ulCap), 0);
	if(iRc > 0) {
		return iRc;
	}
	if(iRc == 0) {
		return -1;   // orderly close by the peer
	}
	return isWouldBlock(WSAGetLastError()) ? 0 : -1;
}

int32_t sendDgram(int32_t lFd, uint32_t ulIp, uint16_t uwPort, const void *pData, uint32_t ulLen) {
	const sockaddr_in sTo = makeAddr(ulIp, uwPort);
	const int iRc = sendto(toSocket(lFd), static_cast<const char *>(pData), static_cast<int>(ulLen), 0, reinterpret_cast<const sockaddr *>(&sTo), sizeof(sTo));
	if(iRc >= 0) {
		return iRc;
	}
	return isWouldBlock(WSAGetLastError()) ? 0 : -1;
}

int32_t recvDgram(int32_t lFd, void *pBuf, uint32_t ulCap, uint32_t &ulIp, uint16_t &uwPort) {
	sockaddr_in sFrom;
	int iLen = sizeof(sFrom);
	const int iRc = recvfrom(toSocket(lFd), static_cast<char *>(pBuf), static_cast<int>(ulCap), 0, reinterpret_cast<sockaddr *>(&sFrom), &iLen);
	if(iRc <= 0) {
		return 0;   // nothing waiting, or an error report of an earlier datagram: either way no datagram now
	}
	ulIp = ntohl(sFrom.sin_addr.s_addr);
	uwPort = ntohs(sFrom.sin_port);
	return iRc;
}

bool localName(int32_t lFd, uint32_t &ulIp, uint16_t &uwPort) {
	sockaddr_in sAddr;
	int iLen = sizeof(sAddr);
	if(getsockname(toSocket(lFd), reinterpret_cast<sockaddr *>(&sAddr), &iLen) != 0) {
		return false;
	}
	ulIp = ntohl(sAddr.sin_addr.s_addr);
	uwPort = ntohs(sAddr.sin_port);
	return true;
}

bool isReady(int32_t lFd, bool isWrite, uint32_t ulMs) {
	fd_set sSet, sErr;
	FD_ZERO(&sSet);
	FD_ZERO(&sErr);
	FD_SET(toSocket(lFd), &sSet);
	FD_SET(toSocket(lFd), &sErr);   // Windows reports a refused non-blocking connect here, never as writable
	timeval sTv;
	sTv.tv_sec = static_cast<long>(ulMs / 1000);
	sTv.tv_usec = static_cast<long>((ulMs % 1000) * 1000);
	const int iRc = isWrite ? select(0, nullptr, &sSet, &sErr, &sTv) : select(0, &sSet, nullptr, &sErr, &sTv);
	return iRc > 0 && (FD_ISSET(toSocket(lFd), &sSet) || FD_ISSET(toSocket(lFd), &sErr));
}

void waitReadable(const int32_t *plFds, uint32_t ulCount, uint32_t ulMs) {
	fd_set sSet;
	FD_ZERO(&sSet);
	uint32_t ulIn = 0;
	for(uint32_t i = 0; i < ulCount && ulIn < FD_SETSIZE; ++i) {
		FD_SET(toSocket(plFds[i]), &sSet);
		++ulIn;
	}
	if(ulIn == 0) {
		return;
	}
	timeval sTv;
	sTv.tv_sec = static_cast<long>(ulMs / 1000);
	sTv.tv_usec = static_cast<long>((ulMs % 1000) * 1000);
	select(0, &sSet, nullptr, nullptr, &sTv);
}

uint32_t resolve(const char *szHost) {
	addrinfo sHints;
	memset(&sHints, 0, sizeof(sHints));
	sHints.ai_family = AF_INET;
	sHints.ai_socktype = SOCK_STREAM;
	addrinfo *pList = nullptr;
	if(getaddrinfo(szHost, nullptr, &sHints, &pList) != 0 || pList == nullptr) {
		return 0xFFFFFFFFu;
	}
	uint32_t ulIp = 0xFFFFFFFFu;
	for(const addrinfo *p = pList; p != nullptr; p = p->ai_next) {
		if(p->ai_family == AF_INET && p->ai_addrlen >= sizeof(sockaddr_in)) {
			ulIp = ntohl(reinterpret_cast<const sockaddr_in *>(p->ai_addr)->sin_addr.s_addr);
			break;
		}
	}
	freeaddrinfo(pList);
	return ulIp;
}

int32_t lastError() { return WSAGetLastError(); }

}  // namespace wsock
}  // namespace rt
